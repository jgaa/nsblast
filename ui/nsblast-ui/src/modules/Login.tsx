import React, { useRef, useState } from 'react';
import { useAppState } from './AppState';
import ErrorBoundary from './ErrorBoundary';


interface LoginInterface {
    setToken: ((token: string) => any);
  }

export default function Login() {

    const loginName = useRef<HTMLInputElement>(null);
    const loginPasswd = useRef<HTMLInputElement>(null);
    const [hasError, setHasError] = useState(false);
    const [errorMsg, setError] = useState("");
    const {getUrl, getAuthHeader, setToken} = useAppState();

    const submit = async (e: React.FormEvent) => {
        e.preventDefault();

        let name :string = loginName.current ? loginName.current.value : "";
        let pass :string = loginPasswd.current ? loginPasswd.current.value : "";

        const token = window.btoa(`${name}:${pass}`);
        const authValue = `basic ${token}`;


        /* Try to access an endpoint using HTTP Basic authentication. If ok,
           the credentials are valid.
        */
        try {
            const res = await fetch(getUrl('/version'), {
                method: "get",
                headers: { Authorization: authValue}
                });

            console.log("fetch res: ", res)

            if (res.ok) {
                setToken(token)
            }

        } catch(error) {
            console.log("Fatch failed: ", error)
            //throw new Error("Failed to validate authentication with server")
            setHasError(true)

            if (error instanceof Error) {
                setError(error.message)
            }
        }
    }

    if (hasError) {
        return (
            <>
            <div className="w3-row" style={{ marginLeft: "20%" }}>
            <div className=' w3-red'>
            <h3>Failed to validate authentication with server</h3>
            <p >{errorMsg}</p>
            </div>
            <p>Refresh to try again!</p>
            </div>
            </>
        )
    }

    return (
        <div className="w3-row" style={{ marginLeft: "22%", width:"30%" }}>

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
