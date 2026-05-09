import React from 'react';
import ReportingDashboard from '@/components/dashboard/reporting-dashboard';

const Reports: React.FC = () => {
  return (
    <div className="min-h-screen bg-gradient-subtle">
      <div className="container mx-auto px-6 py-8">
        <ReportingDashboard />
      </div>
    </div>
  );
};

export default Reports;