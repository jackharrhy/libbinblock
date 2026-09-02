import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { createBrowserRouter } from 'react-router';
import { RouterProvider } from 'react-router/dom';

import '@fontsource-variable/source-code-pro/wght.css';
import { AppShell, NotFoundPage, RouteErrorPage } from './app-shell.js';
import { ArchitecturePage } from './pages/architecture-page.js';
import { OverviewPage } from './pages/overview-page.js';
import './style.css';

const router = createBrowserRouter([
  {
    path: '/',
    Component: AppShell,
    ErrorBoundary: RouteErrorPage,
    children: [
      { index: true, Component: OverviewPage },
      { path: 'architecture', Component: ArchitecturePage },
      {
        path: 'playground',
        lazy: async () => {
          const { PlaygroundPage } = await import('./pages/playground-page.js');
          return { Component: PlaygroundPage };
        },
      },
      { path: '*', Component: NotFoundPage },
    ],
  },
]);

const root = document.querySelector('#root');
if (!(root instanceof HTMLElement)) throw new Error('The application root is missing.');

createRoot(root).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
