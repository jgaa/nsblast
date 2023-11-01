import { BrowserRouter as Router } from 'react-router-dom';
import { Navigation } from './pages/Navigation';
import AppState from './modules/AppState';
import RightPane from './modules/RightPane';
import ErrorBoundary from './modules/ErrorBoundary';
import ErrorScreen from "./modules/ErrorScreen"

export default function App() {

  return (
    <Router>
      <AppState>
        <ErrorBoundary fallback={ErrorScreen}>
          <Navigation />
          <RightPane />
        </ErrorBoundary>
      </AppState>
    </Router>
  )
}
