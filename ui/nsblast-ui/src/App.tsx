import { BrowserRouter, HashRouter } from 'react-router-dom';
import { Navigation } from './pages/Navigation';
import AppState from './modules/AppState';
import RightPane from './modules/RightPane';
import { UI_ROUTER_BASENAME, UI_ROUTER_MODE } from './config';

export default function App() {
  const Router = UI_ROUTER_MODE === 'hash' ? HashRouter : BrowserRouter;

  return (
    <Router basename={UI_ROUTER_BASENAME}>
      <AppState>
        <Navigation />
        <RightPane />
      </AppState>
    </Router>
  )
}
