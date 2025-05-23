import React from 'react';
import { Routes, Route, Navigate } from "react-router-dom";
import Dashboard from '../pages/Dashboard';
import Login from './Login';
import { useAppState } from './AppState'
import ResourceRecords from '../pages/ResourceRecords';
import Roles from '../pages/Roles';

function LoggedIn() {
    return (
        <div className="w3-row" style={{ marginLeft: "25%" }}>
            <Routes>
                <Route path="/" element={<Navigate to="dashboard" />} />
                <Route path="/admin" element={<h1>admin</h1>} />
                <Route path="/dashboard" exact element={<Dashboard />} />
                <Route path="/dashboard/rr" element={<ResourceRecords />} />
                <Route path="/tenants" element={<h1>tenants</h1>} />
                <Route path="/roles" element={<Roles/>} />
                <Route path="/users" element={<h1>users</h1>} />
                <Route path="/apikeys" element={<h1>apikeys</h1>} />
                <Route path="/events" element={<h1>events</h1>} />
                <Route path="/log" element={<h1>log</h1>} />
                <Route path="/about" element={<h1>about</h1>} />

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

