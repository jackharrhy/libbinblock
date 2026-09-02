import { useEffect } from 'react';
import { Link, NavLink, Outlet, ScrollRestoration, useLocation, useRouteError } from 'react-router';

const pageTitles: Record<string, string> = {
  '/': 'libbinblock | BinScript and Bin Blocks',
  '/architecture': 'Implementation | libbinblock',
  '/playground': 'Editor | libbinblock',
};

export function AppShell() {
  const location = useLocation();
  const isPlayground = location.pathname === '/playground';

  useEffect(() => {
    document.title = pageTitles[location.pathname] ?? 'libbinblock';
  }, [location.pathname]);

  return (
    <div className={isPlayground ? 'site-shell site-shell--playground' : 'site-shell'}>
      <a className="skip-link" href="#main-content">
        Skip to content
      </a>
      <header className="site-header">
        <NavLink className="site-brand" to="/" end>
          <strong>libbinblock</strong>
          <span>BinScript and Bin Block library</span>
        </NavLink>
        <nav className="site-nav" aria-label="Primary navigation">
          <NavLink to="/" end>
            Overview
          </NavLink>
          <NavLink to="/architecture">Architecture</NavLink>
          <a href="https://github.com/jackharrhy/libbinblock">Source</a>
          <NavLink className="site-nav-playground" to="/playground">
            Editor
          </NavLink>
        </nav>
      </header>
      <main id="main-content" className={isPlayground ? 'site-main site-main--playground' : 'site-main'}>
        <Outlet />
      </main>
      <ScrollRestoration />
    </div>
  );
}

export function NotFoundPage() {
  return (
    <section className="route-message page-width">
      <p className="page-kicker">Not found</p>
      <h1>There is no page at this address.</h1>
      <Link className="button button--primary" to="/">
        Go to the overview
      </Link>
    </section>
  );
}

export function RouteErrorPage() {
  const error = useRouteError();
  return (
    <section className="route-message page-width" role="alert">
      <p className="page-kicker">Something went wrong</p>
      <h1>This page could not load.</h1>
      <p>{error instanceof Error ? error.message : String(error)}</p>
      <a className="button button--primary" href="/">
        Reload the site
      </a>
    </section>
  );
}
