import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import App from './App';

describe('App', () => {
  it('renders app title', () => {
    window.history.pushState({}, 'test ui', '/ui/');
    render(<App />);
    expect(screen.getByText(/nsBLAST/i)).toBeInTheDocument();
  });
});
