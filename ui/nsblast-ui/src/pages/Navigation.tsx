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
    FaFileCircleQuestion,
    FaNetworkWired

} from "react-icons/fa6"
import { useAppState } from '../modules/AppState';
import { detectDynIpAccess } from '../modules/dynip';

export interface INavigationProps {
}

export function Navigation(props: INavigationProps) {
  const { state, api } = useAppState();
  const loggedIn = state.loginToken.length > 0;
  const [canShowLog, setCanShowLog] = React.useState(false);
  const [canShowDynIp, setCanShowDynIp] = React.useState(false);

  React.useEffect(() => {
    let active = true;

    if (!loggedIn) {
      setCanShowLog(false);
      setCanShowDynIp(false);
      return () => {
        active = false;
      };
    }

    const probeNavigationAccess = async () => {
      try {
        const [logAccess, dynIpAccess] = await Promise.allSettled([
          api.request('/log/show', {
            method: 'GET',
            parse: 'none',
            retry: false,
            retries: 0
          }),
          detectDynIpAccess(api)
        ]);

        if (!active) {
          return;
        }

        setCanShowLog(logAccess.status === 'fulfilled');
        if (dynIpAccess.status === 'fulfilled') {
          setCanShowDynIp(dynIpAccess.value.canList && dynIpAccess.value.canProvision);
        } else {
          setCanShowDynIp(false);
        }
      } catch {
        if (!active) {
          return;
        }
        setCanShowLog(false);
        setCanShowDynIp(false);
      }
    };

    void probeNavigationAccess();

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
          {canShowDynIp && <Link className='w3-bar-item w3-button' to="dynip"><FaNetworkWired/> DynIP</Link>}
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
