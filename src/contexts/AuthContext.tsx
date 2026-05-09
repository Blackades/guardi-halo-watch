import React, { createContext, useContext, useState, useEffect, ReactNode } from 'react';
import { jwtDecode } from 'jwt-decode';
import Cookies from 'js-cookie';
import { apiConfig } from '@/lib/api';

export interface User {
  id: string;
  username: string;
  fullName: string;
  email: string;
  role: 'admin' | 'nurse' | 'doctor' | 'viewer';
}

interface JWTPayload {
  sub: string;
  username: string;
  full_name: string;
  email: string;
  role: string;
  exp: number;
}

interface AuthContextType {
  user: User | null;
  isAuthenticated: boolean;
  isLoading: boolean;
  login: (username: string, password: string) => Promise<void>;
  logout: () => void;
  refreshToken: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

export const useAuth = () => {
  const context = useContext(AuthContext);
  if (context === undefined) {
    throw new Error('useAuth must be used within an AuthProvider');
  }
  return context;
};

interface AuthProviderProps {
  children: ReactNode;
}

export const AuthProvider: React.FC<AuthProviderProps> = ({ children }) => {
  const [user, setUser] = useState<User | null>(null);
  const [isLoading, setIsLoading] = useState(true);

  const decodeToken = (token: string): User | null => {
    try {
      const decoded = jwtDecode<JWTPayload>(token);
      
      // Check if token is expired
      if (decoded.exp * 1000 < Date.now()) {
        return null;
      }

      return {
        id: decoded.sub,
        username: decoded.username,
        fullName: decoded.full_name,
        email: decoded.email,
        role: decoded.role as User['role']
      };
    } catch (error) {
      console.error('Error decoding token:', error);
      return null;
    }
  };

  const login = async (username: string, password: string): Promise<void> => {
    try {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/auth/login`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ username, password }),
      });

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.detail || 'Login failed');
      }

      const data = await response.json();
      const { access_token, refresh_token } = data;

      // Store tokens in cookies
      Cookies.set('access_token', access_token, { 
        expires: 1, // 1 day
        secure: true,
        sameSite: 'strict'
      });
      Cookies.set('refresh_token', refresh_token, { 
        expires: 7, // 7 days
        secure: true,
        sameSite: 'strict'
      });

      // Decode and set user
      const decodedUser = decodeToken(access_token);
      if (decodedUser) {
        setUser(decodedUser);
      } else {
        throw new Error('Invalid token received');
      }
    } catch (error) {
      console.error('Login error:', error);
      throw error;
    }
  };

  const logout = () => {
    Cookies.remove('access_token');
    Cookies.remove('refresh_token');
    setUser(null);
  };

  const refreshToken = async (): Promise<void> => {
    try {
      const refreshTokenValue = Cookies.get('refresh_token');
      if (!refreshTokenValue) {
        throw new Error('No refresh token available');
      }

      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/auth/refresh`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ refresh_token: refreshTokenValue }),
      });

      if (!response.ok) {
        throw new Error('Token refresh failed');
      }

      const data = await response.json();
      const { access_token } = data;

      // Update access token
      Cookies.set('access_token', access_token, { 
        expires: 1,
        secure: true,
        sameSite: 'strict'
      });

      // Decode and update user
      const decodedUser = decodeToken(access_token);
      if (decodedUser) {
        setUser(decodedUser);
      }
    } catch (error) {
      console.error('Token refresh error:', error);
      logout();
      throw error;
    }
  };

  // Initialize auth state on mount
  useEffect(() => {
    const initializeAuth = async () => {
      try {
        const accessToken = Cookies.get('access_token');
        if (accessToken) {
          const decodedUser = decodeToken(accessToken);
          if (decodedUser) {
            setUser(decodedUser);
          } else {
            // Token expired, try to refresh
            await refreshToken();
          }
        }
      } catch (error) {
        console.error('Auth initialization error:', error);
        logout();
      } finally {
        setIsLoading(false);
      }
    };

    initializeAuth();
  }, []);

  // Auto-refresh token before expiration
  useEffect(() => {
    if (!user) return;

    const accessToken = Cookies.get('access_token');
    if (!accessToken) return;

    try {
      const decoded = jwtDecode<JWTPayload>(accessToken);
      const expirationTime = decoded.exp * 1000;
      const currentTime = Date.now();
      const timeUntilExpiration = expirationTime - currentTime;
      
      // Refresh token 5 minutes before expiration
      const refreshTime = timeUntilExpiration - (5 * 60 * 1000);
      
      if (refreshTime > 0) {
        const timeoutId = setTimeout(() => {
          refreshToken().catch(console.error);
        }, refreshTime);

        return () => clearTimeout(timeoutId);
      }
    } catch (error) {
      console.error('Error setting up token refresh:', error);
    }
  }, [user]);

  const value: AuthContextType = {
    user,
    isAuthenticated: !!user,
    isLoading,
    login,
    logout,
    refreshToken,
  };

  return (
    <AuthContext.Provider value={value}>
      {children}
    </AuthContext.Provider>
  );
};