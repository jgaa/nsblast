import React, { useRef, useState } from 'react';
import { useAppState } from './AppState';
import { ALLOW_HTTP_LOGIN } from '../config';

const isLocalHostname = (hostname: string): boolean =>
    hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '::1';

export default function Login() {

    const loginName = useRef<HTMLInputElement>(null);
    const loginPasswd = useRef<HTMLInputElement>(null);
    const [hasError, setHasError] = useState(false);
    const [errorMsg, setError] = useState("");
    const { api, setToken, state } = useAppState();
    const localUiHost = isLocalHostname(window.location.hostname);
    const allowInsecureLocalLogin = ALLOW_HTTP_LOGIN && localUiHost;

    const submit = async (e: React.FormEvent) => {
        e.preventDefault();

        if (!allowInsecureLocalLogin && window.location.protocol !== 'https:' && !localUiHost) {
            setHasError(true);
            setError('HTTPS is required for production login. Serve the UI over HTTPS.');
            return;
        }
        if (!allowInsecureLocalLogin && /^http:\/\//i.test(state.api) && !/^http:\/\/(localhost|127\.0\.0\.1|::1)(:\d+)?(\/|$)/i.test(state.api)) {
            setHasError(true);
            setError('Insecure API URL detected. Use HTTPS for production API endpoints.');
            return;
        }

        const name :string = loginName.current ? loginName.current.value : "";
        const pass :string = loginPasswd.current ? loginPasswd.current.value : "";

        const token = window.btoa(`${name}:${pass}`);
        const authValue = `Basic ${token}`;


        /* Try to access an endpoint using HTTP Basic authentication. If ok,
           the credentials are valid.
        */
        try {
            await api.request('/version', {
                method: 'GET',
                authorization: authValue
            });
            setToken(token);

            // Reset the zones state
            window.localStorage.setItem('zones.current', "");

        } catch(error) {
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
        <div className="w3-row ns-shell-content">

            <div className='w3-card ns-login-panel'>
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
