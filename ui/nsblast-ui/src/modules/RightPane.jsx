import React from 'react';
import { Routes, Route, Navigate } from "react-router-dom";
import Dashboard from '../pages/Dashboard';
import Login from './Login';
import { useAppState } from './AppState'
import ResourceRecords from '../pages/ResourceRecords';
import Roles from '../pages/Roles';
import Tenants from '../pages/Tenants';
import Users from '../pages/Users';
import NotImplementedPage from '../pages/NotImplementedPage';

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
                <Route path="/admin" element={<NotImplementedPage title="Admin Console" />} />
                <Route path="/apikeys" element={<NotImplementedPage title="API Keys" />} />
                <Route path="/events" element={<NotImplementedPage title="Events" />} />
                <Route path="/log" element={<NotImplementedPage title="Log" />} />
                <Route path="/about" element={<NotImplementedPage title="About" description="UI placeholder until backend/system details endpoint is available." />} />

            </Routes>
        </div>
    );
}

export default function RightPane() {
    let { isLoggedIn, setToken } = useAppState()
    if (isLoggedIn()) {
        return <LoggedIn />;
    }

    return <Login />
}
