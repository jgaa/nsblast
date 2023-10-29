import * as React from 'react';
import TenantStats from '../modules/TenantStats';

export interface IDashboardProps {
}

export default function Dashboard (props: IDashboardProps) {
  return (
    <>
    <h1>Dashboard</h1>
    <TenantStats/>
    </>
  );
}
