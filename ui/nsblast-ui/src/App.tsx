import React, {useState} from 'react';
import { Routes, Route, Navigate } from "react-router-dom";
import logo from './logo.svg';
import './App.css';
import { Navigation } from './pages/Navigation';
import Dashboard from './pages/Dashboard';
import Login from './modules/Login';


function LoggedIn() {
  return (
    <div className="w3-row" style={{marginLeft:"25%"}}>
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

// function Login(setToken : any ) {

//   return (
//     <div className="w3-row" style={{marginLeft:"25%"}}>
//     <p>Modal Login Dialog here</p>
//     </div>
//   )
// }

function App() {
  const [token, setToken] = useState("");
  console.log(`In App(): token is: "${token}"`)
  if (token === "") {
    return <Login setToken={setToken}/>
  }
  return (
      <LoggedIn/>  
  );
}

export default App;

/*
https://betterprogramming.pub/how-to-authentication-users-with-token-in-a-react-application-f99997c2ee9d

*/