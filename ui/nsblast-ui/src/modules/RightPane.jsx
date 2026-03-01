import React, { useEffect } from 'react';
import { Routes, Route, Navigate } from "react-router-dom";
import Dashboard from '../pages/Dashboard';
import Login from './Login';
import { useAppState } from './AppState'
import ResourceRecords from '../pages/ResourceRecords';
import Roles from '../pages/Roles';
import Tenants from '../pages/Tenants';
import Users from '../pages/Users';
import NotImplementedPage from '../pages/NotImplementedPage';
import About from '../pages/About';
import LogView from '../pages/LogView';
import DynIp from '../pages/DynIp';
import Backups from '../pages/Backups';

function LoggedIn() {
    return (
        <div className="w3-row" style={{ marginLeft: "25%" }}>
            <Routes>
                <Route path="/" element={<Navigate to="dashboard" />} />
                <Route path="/dashboard" exact element={<Dashboard />} />
                <Route path="/dashboard/rr" element={<ResourceRecords />} />
                <Route path="/tenants" element={<Tenants/>} />
                <Route path="/roles" element={<Roles/>} />
                <Route path="/users" element={<Users/>} />
                <Route path="/dynip" element={<DynIp/>} />
                <Route path="/backups" element={<Backups/>} />
                <Route path="/admin" element={<NotImplementedPage title="Admin Console" />} />
                <Route path="/apikeys" element={<NotImplementedPage title="API Keys" />} />
                <Route path="/events" element={<NotImplementedPage title="Events" />} />
                <Route path="/log" element={<LogView />} />
                <Route path="/about" element={<About />} />

            </Routes>
        </div>
    );
}

export default function RightPane() {
    let { isLoggedIn, setToken, api } = useAppState()

    useEffect(() => {
        if (!isLoggedIn()) {
            return;
        }

        let active = true;
        const validateSession = async () => {
            try {
                await api.request('/version', {
                    method: 'GET',
                    parse: 'none',
                    retry: false,
                    retries: 0
                });
            } catch (error) {
                if (
                    active &&
                    typeof error === 'object' &&
                    error !== null &&
                    (error.status === 401 || error.status === 403)
                ) {
                    setToken('');
                }
            }
        };

        void validateSession();
        return () => {
            active = false;
        };
    }, [api, isLoggedIn, setToken]);

    if (isLoggedIn()) {
        return <LoggedIn />;
    }

    return <Login />
}
