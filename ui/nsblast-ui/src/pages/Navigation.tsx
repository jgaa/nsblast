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
import { useAppState } from '../modules/AppState';

export interface INavigationProps {
}

export function Navigation(props: INavigationProps) {
  const { state, api } = useAppState();
  const loggedIn = state.loginToken.length > 0;
  const [canShowLog, setCanShowLog] = React.useState(false);

  React.useEffect(() => {
    let active = true;

    if (!loggedIn) {
      setCanShowLog(false);
      return () => {
        active = false;
      };
    }

    const probeLogAccess = async () => {
      try {
        await api.request('/log/show', {
          method: 'GET',
          parse: 'none',
          retry: false,
          retries: 0
        });
        if (active) {
          setCanShowLog(true);
        }
      } catch {
        if (active) {
          setCanShowLog(false);
        }
      }
    };

    void probeLogAccess();

    return () => {
      active = false;
    };
  }, [api, loggedIn, state.loginToken]);

  if (loggedIn) {
    return (
      <div className='w3-theme-d4 w3-sidebar w3-border w3-bar-block'>
        <div className="ns-brand-header">
          <div className="ns-brand-logo" role="img" aria-label="nsBLAST logo"></div>
          <h1 className="ns-brand-title">nsBLAST</h1>
        </div>
        <nav>
          <Link className='w3-bar-item w3-button' to="dashboard"><FaChartSimple/> Dashboard</Link>
          <Link className='w3-bar-item w3-button' to="admin"><FaCrown/> Admin Console</Link>
          <Link className='w3-bar-item w3-button' to="tenants"><FaHouseUser/> Tenants</Link>
          <Link className='w3-bar-item w3-button' to="roles"><FaHouseLock/> Roles</Link>
          <Link className='w3-bar-item w3-button' to="users"><FaUsers/> Users</Link>
          <Link className='w3-bar-item w3-button' to="apikeys"><FaKey/> API Keys</Link>
          <Link className='w3-bar-item w3-button' to="events"><FaRectangleList/> Events</Link>
          {canShowLog && <Link className='w3-bar-item w3-button' to="log"><FaFileLines/> Log</Link>}
          <Link className='w3-bar-item w3-button' to="about"><FaFileCircleQuestion/> About</Link>
        </nav>
      </div>
    );
  }

  return (
    <div className='w3-theme-d4 w3-sidebar w3-border w3-bar-block'>
      <div className="ns-brand-header">
        <div className="ns-brand-logo" role="img" aria-label="nsBLAST logo"></div>
        <h1 className="ns-brand-title">nsBLAST</h1>
      </div>
      </div>
  );
}
