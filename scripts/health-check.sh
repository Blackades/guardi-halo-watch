#!/bin/bash

# Health check script for Halo Watch system
# This script checks the health of all system components

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BACKEND_URL="${BACKEND_URL:-http://localhost:8000}"
FRONTEND_URL="${FRONTEND_URL:-http://localhost:3000}"
TIMEOUT=10

echo "🏥 Halo Watch System Health Check"
echo "=================================="

# Function to check HTTP endpoint
check_endpoint() {
    local name=$1
    local url=$2
    local expected_status=${3:-200}
    
    echo -n "Checking $name... "
    
    if response=$(curl -s -o /dev/null -w "%{http_code}" --max-time $TIMEOUT "$url" 2>/dev/null); then
        if [ "$response" = "$expected_status" ]; then
            echo -e "${GREEN}✓ OK${NC} (HTTP $response)"
            return 0
        else
            echo -e "${YELLOW}⚠ WARNING${NC} (HTTP $response, expected $expected_status)"
            return 1
        fi
    else
        echo -e "${RED}✗ FAILED${NC} (Connection failed)"
        return 1
    fi
}

# Function to check Docker container
check_container() {
    local container_name=$1
    echo -n "Checking container $container_name... "
    
    if docker ps --format "table {{.Names}}" | grep -q "^$container_name$"; then
        local status=$(docker inspect --format='{{.State.Health.Status}}' "$container_name" 2>/dev/null || echo "no-healthcheck")
        case $status in
            "healthy")
                echo -e "${GREEN}✓ HEALTHY${NC}"
                return 0
                ;;
            "unhealthy")
                echo -e "${RED}✗ UNHEALTHY${NC}"
                return 1
                ;;
            "starting")
                echo -e "${YELLOW}⚠ STARTING${NC}"
                return 1
                ;;
            "no-healthcheck")
                echo -e "${GREEN}✓ RUNNING${NC} (no health check configured)"
                return 0
                ;;
            *)
                echo -e "${YELLOW}⚠ UNKNOWN${NC} ($status)"
                return 1
                ;;
        esac
    else
        echo -e "${RED}✗ NOT RUNNING${NC}"
        return 1
    fi
}

# Check system components
echo ""
echo "🔍 Checking System Components:"
echo "------------------------------"

# Initialize counters
total_checks=0
failed_checks=0

# Check Docker containers if available
if command -v docker &> /dev/null; then
    containers=("halo-watch-backend" "halo-watch-frontend" "halo-watch-nginx")
    for container in "${containers[@]}"; do
        total_checks=$((total_checks + 1))
        if ! check_container "$container"; then
            failed_checks=$((failed_checks + 1))
        fi
    done
fi

echo ""
echo "🌐 Checking HTTP Endpoints:"
echo "---------------------------"

# Check backend health endpoint
total_checks=$((total_checks + 1))
if ! check_endpoint "Backend Health" "$BACKEND_URL/api/v1/system/status"; then
    failed_checks=$((failed_checks + 1))
fi

# Check backend API
total_checks=$((total_checks + 1))
if ! check_endpoint "Backend API" "$BACKEND_URL/api/v1/overview"; then
    failed_checks=$((failed_checks + 1))
fi

# Check frontend
total_checks=$((total_checks + 1))
if ! check_endpoint "Frontend" "$FRONTEND_URL"; then
    failed_checks=$((failed_checks + 1))
fi

# Check WebSocket endpoint (basic connectivity)
echo -n "Checking WebSocket endpoint... "
if timeout $TIMEOUT bash -c "</dev/tcp/localhost/8000" 2>/dev/null; then
    echo -e "${GREEN}✓ OK${NC} (Port accessible)"
else
    echo -e "${RED}✗ FAILED${NC} (Port not accessible)"
    failed_checks=$((failed_checks + 1))
fi
total_checks=$((total_checks + 1))

echo ""
echo "📊 Health Check Summary:"
echo "------------------------"

passed_checks=$((total_checks - failed_checks))
echo "Total checks: $total_checks"
echo -e "Passed: ${GREEN}$passed_checks${NC}"
echo -e "Failed: ${RED}$failed_checks${NC}"

if [ $failed_checks -eq 0 ]; then
    echo -e "\n${GREEN}🎉 All systems operational!${NC}"
    exit 0
elif [ $failed_checks -lt $((total_checks / 2)) ]; then
    echo -e "\n${YELLOW}⚠ Some issues detected, but system is mostly operational${NC}"
    exit 1
else
    echo -e "\n${RED}🚨 Critical issues detected! System may not be operational${NC}"
    exit 2
fi