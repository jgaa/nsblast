import React, { Component } from "react";
import ErrorScreen from './ErrorScreen';

export default class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { hasError: false, error: "" };

  }

  static getDerivedStateFromError(error) {
    // Update state so the next render will show the fallback UI.
    return { hasError: true };
  }

  componentDidCatch(error, info) {
    // Example "componentStack":
    //   in ComponentThatThrows (created by App)
    //   in ErrorBoundary (created by App)
    //   in div (created by App)
    //   in App
    //logErrorToMyService(error, info.componentStack);
    console.log('ErrorBoundary: Caught: ', error)
    //this.state.error = error.message;
  }

  render() {
    if (this.state.hasError) {
      // You can render any custom fallback UI
      return this.props.fallback;
      //return <ErrorScreen/>
    }

    return this.props.children;
  }
}
