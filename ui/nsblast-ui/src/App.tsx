import React from 'react';
import { Routes, Route, Navigate } from "react-router-dom";
import logo from './logo.svg';
import './App.css';
import { Navigation } from './pages/Navigation';
import Dashboard from './pages/Dashboard';

function App() {
  return (
    <div className="App" style={{marginLeft:"25%"}}>
      <Routes>
        <Route path="/" element={<Navigate to="dashboard"/>} />
        <Route path="/admin" element={<h1>admin</h1>} />
        <Route path="/dashboard" element={<Dashboard/>} />
        <Route path="/tenants" element={<h1>tenants</h1>} />
        <Route path="/roles" element={<h1>roles</h1>} />
        <Route path="/users" element={<h1>users</h1>} />
        <Route path="/apikeys" element={<h1>apikeys</h1>} />
        <Route path="/events" element={<h1>events</h1>} />
        <Route path="/log" element={<h1>log</h1>} />
        <Route path="/about" element={<h1>about</h1>} />
      </Routes>
    </div>
  );
}

export default App;
