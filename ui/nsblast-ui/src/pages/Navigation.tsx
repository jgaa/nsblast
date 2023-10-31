import * as React from 'react';
import { Link } from "react-router-dom";
import {
    FaUsers,
    FaCrown,
    FaChartSimple,
    FaHouseUser,
    FaHouseLock,
    FaKey,
    FaRectangleList,
    FaFileLines,
    FaFileCircleQuestion

} from "react-icons/fa6"

export interface INavigationProps {
}

export function Navigation(props: INavigationProps) {
  return (
    <div className='w3-theme-d4 w3-sidebar w3-border w3-bar-block ' style={{width:"20%"}}>
      <h1>nsBLAST</h1>
      <nav>
        <Link className='w3-bar-item w3-button' to="dashboard"><FaChartSimple/> Dashboard</Link>
        <Link className='w3-bar-item w3-button' to="admin"><FaCrown/> Admin Console</Link>
        <Link className='w3-bar-item w3-button' to="tenants"><FaHouseUser/> Tenants</Link>
        <Link className='w3-bar-item w3-button' to="roles"><FaHouseLock/> Roles</Link>
        <Link className='w3-bar-item w3-button' to="users"><FaUsers/> Users</Link>
        <Link className='w3-bar-item w3-button' to="apikeys"><FaKey/> API Keys</Link>
        <Link className='w3-bar-item w3-button' to="events"><FaRectangleList/> Events</Link>
        <Link className='w3-bar-item w3-button' to="log"><FaFileLines/> Log</Link>
        <Link className='w3-bar-item w3-button' to="about"><FaFileCircleQuestion/> About</Link>
      </nav>
    </div>
  );
}
