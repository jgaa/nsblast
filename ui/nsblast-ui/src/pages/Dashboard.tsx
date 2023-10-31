import * as React from 'react';
import TenantStats from '../modules/TenantStats';
import LatestEvents from '../modules/LatestEvents';
import { Zones } from '../modules/Zones';

export interface IDashboardProps {
}

export default function Dashboard (props: IDashboardProps) {
  return (
    <>
    <h1>Dashboard</h1>
    <div className='w3-cell-row'>
    <TenantStats/>
    <LatestEvents/>
    </div>
    <Zones/>
    </>
  );
}
