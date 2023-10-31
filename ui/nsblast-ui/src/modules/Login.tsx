import React, { useRef, Suspense, Dispatch } from 'react';

interface LoginInterface {
    setToken: ((token: string) => any);
  }

export default function Login({setToken} : LoginInterface) {

    const loginName = useRef<HTMLInputElement>(null);
    const loginPasswd = useRef<HTMLInputElement>(null);

    console.log(`setToken is: ${setToken}`)

    const submit = (e: React.FormEvent) => {
        e.preventDefault();

        let name :string = loginName.current ? loginName.current.value : "";
        let pass :string = loginPasswd.current ? loginPasswd.current.value : "";

        console.log(`Login event: ${name} : ${pass}`)

        const token = window.btoa(`${name}:${pass}`);
        const authHeader = `basic ${token}`;
        console.log(`auth header: ${authHeader}`);

        fetch(`http://127.0.0.1:8080/api/v1/version`, {
            method: "get",
            headers: {
                Authorization: authHeader
            }
        })
        .then(response => response.json())
        .then(setToken(authHeader))
        .catch(console.error)
    }

    return (
        <div className="w3-row" style={{ marginLeft: "25%", width:"30%" }}>

            <div className='w3-card' style={{marginTop: "5%"}}>
                <header className="w3-container w3-blue">
                    <h1>Login</h1>
                </header>

                <form className="w3-container" onSubmit={submit}>
                    <div className="w3-section">
                        <label><b>Username</b></label>
                        <input ref={loginName} className="w3-input w3-border w3-margin-bottom" type="text" 
                        placeholder="Enter Username" required />
                        <label><b>Password</b></label>
                        <input ref={loginPasswd} className="w3-input w3-border" type="password" 
                        placeholder="Enter Password" required />
                        <button className="w3-button w3-block w3-green w3-section w3-padding" type="submit">Login</button>
                    </div>
                </form>

            </div>
        </div>

    );
}
