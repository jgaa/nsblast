import React, { createContext, useContext, useState } from "react";
import { API_BASE_URL } from "../config";
import { createApiClient } from "./apiClient";

export const AppStateContext = createContext();

const trimTrailingSlash = (value) => value.replace(/\/+$/, "");
const trimLeadingSlash = (value) => value.replace(/^\/+/, "");

const joinUrl = (base, target) => {
    const normalizedBase = trimTrailingSlash(base || "");
    const normalizedTarget = trimLeadingSlash(target || "");

    if (!normalizedBase) {
        return normalizedTarget ? `/${normalizedTarget}` : "/";
    }

    if (!normalizedTarget) {
        return normalizedBase;
    }

    return `${normalizedBase}/${normalizedTarget}`;
};

export default function AppState({ children }) {
    const initialState = {
        loginToken: "",
        api: API_BASE_URL,
    }

    const [state, setState] = useState(initialState);

    const isLoggedIn = () => state.loginToken.length > 0;

    const setToken = (token) => {
        setState((prev) => ({...prev, loginToken: token}));
    };

    const getUrl = (target) => joinUrl(state.api, target);

    const getAuthHeader = () => {
        if (!state.loginToken) {
            return {};
        }
        return { Authorization: `Basic ${state.loginToken}` };
    };

    const api = createApiClient({
        baseUrl: state.api,
        getToken: () => state.loginToken
    });

    return (
        <AppStateContext.Provider value={{state, getUrl, isLoggedIn, setToken, getAuthHeader, api}}>
            {children}
        </AppStateContext.Provider>
    )
}

export const useAppState = () => useContext(AppStateContext)
