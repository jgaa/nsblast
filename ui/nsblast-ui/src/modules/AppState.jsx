import React, { createContext, useContext, useState } from "react";

export const AppStateContext = createContext();


export default function AppState({ children }) {
    const initialState = {
        loginToken: "",
        api: "http://127.0.0.1:8080/api/v1",
    }

    const [state, setState] = useState(initialState);

    const isLoggedIn = () => state.loginToken.length > 0;

    const setToken = (token) => {
        console.log(`Setting auth token to: ${token}`)
        setState({...state, loginToken: token});
    }

    const getUrl = (target) => state.api + target;

    const getAuthHeader = () => {
        const ah = { Authorization: ` Basic ${state.loginToken}`}
        console.log("State is: ", state);
        console.log(`Returninmg auth header: `, ah)
        return ah;
    }

    return (
        <AppStateContext.Provider value={{state, getUrl, isLoggedIn, setToken, getAuthHeader}}>
            {children}
        </AppStateContext.Provider>
    )
}

export const useAppState = () => useContext(AppStateContext)
