import { Link } from 'react-router';

import { gradientExampleHtml, layeredExampleHtml, maskExampleHtml, overviewExampleHtml } from '../binscript-highlight.generated.js';
import { BinScriptCode } from '../components/binscript-code.js';
import { ExampleRender, useExampleRenders } from '../components/example-render.js';

const recipes = [
  {
    id: 'gradient' as const,
    title: 'Place color stops',
    description: (
      <p>
        <code>lg</code> makes a linear gradient. The first argument is its angle, and percentages pin individual color stops.
      </p>
    ),
    html: gradientExampleHtml,
    caption: 'A three-stop linear gradient.',
  },
  {
    id: 'mask' as const,
    title: 'Shape alpha with a mask',
    description: (
      <p>
        <code>rg</code> makes a radial gradient. Named arguments set its center and radius, then <code>mask</code> applies its alpha.
      </p>
    ),
    html: maskExampleHtml,
    caption: 'Blue ink shaped by a soft radial mask.',
  },
  {
    id: 'layers' as const,
    title: 'Put one image over another',
    description: (
      <p>
        <code>over</code> composites its argument above the current image. Its optional second argument controls opacity.
      </p>
    ),
    html: layeredExampleHtml,
    caption: 'A soft light composited over a blue gradient.',
  },
] as const;

export function OverviewPage() {
  const renders = useExampleRenders();

  return (
    <article className="overview-page page-width">
      <header className="overview-intro">
        <div>
          <h1>BinScript and Bin Blocks</h1>
          <p className="lead">Write small image recipes with colors, gradients, collections, masks, and layers.</p>
          <div className="text-actions">
            <Link to="/playground">Open the editor</Link>
            <Link to="/architecture">Architecture</Link>
            <a href="https://github.com/jackharrhy/libbinblock">Source repository</a>
          </div>
        </div>
        <dl className="repo-summary">
          <div>
            <dt>Binding</dt>
            <dd>
              <code>name := value</code>
            </dd>
          </div>
          <div>
            <dt>Color</dt>
            <dd>
              <code>#rrggbb[aa]</code>
            </dd>
          </div>
          <div>
            <dt>Output</dt>
            <dd>an image expression on its own line</dd>
          </div>
        </dl>
      </header>

      <section className="example-section" aria-labelledby="example-heading">
        <div className="section-copy">
          <h2 id="example-heading">Build a collection</h2>
          <p>
            <code>palette</code> creates keyed colors. <code>map(fill)</code> turns each color into an image, and image methods apply to the
            whole collection.
          </p>
        </div>
        <div className="example-layout">
          <BinScriptCode className="source-code" html={overviewExampleHtml} label="BinScript collection example" />
          <ExampleRender result={renders.collections} caption="Four fills, followed by the same collection with a gradient mask." />
        </div>
      </section>

      <section className="recipe-section" aria-labelledby="recipes-heading">
        <div className="section-copy">
          <h2 id="recipes-heading">More image recipes</h2>
          <p>Each program below is compiled and rendered in this page.</p>
        </div>
        <div className="recipe-list">
          {recipes.map((recipe) => (
            <article className="recipe-example" key={recipe.id}>
              <header>
                <h3>{recipe.title}</h3>
                {recipe.description}
              </header>
              <div className="recipe-pair">
                <BinScriptCode className="source-code" html={recipe.html} label={recipe.title + ' BinScript example'} />
                <ExampleRender result={renders[recipe.id]} caption={recipe.caption} />
              </div>
            </article>
          ))}
        </div>
      </section>

      <section className="syntax-section" aria-labelledby="syntax-heading">
        <div className="section-copy">
          <h2 id="syntax-heading">Syntax at a glance</h2>
          <p>Values are immutable. Calls compose from left to right, and a standalone image expression publishes an output.</p>
        </div>
        <dl className="syntax-rules">
          <div>
            <dt>Bind</dt>
            <dd>
              <code>image := fill(red)</code>
            </dd>
          </div>
          <div>
            <dt>Chain</dt>
            <dd>
              <code>image.size(64).opacity(0.5)</code>
            </dd>
          </div>
          <div>
            <dt>Lift</dt>
            <dd>
              <code>colors.map(fill).size(64)</code>
            </dd>
          </div>
          <div>
            <dt>Publish</dt>
            <dd>
              <code>image</code>
            </dd>
          </div>
        </dl>
        <p className="syntax-next">
          <Link to="/playground">Edit a program in the browser</Link> or <Link to="/architecture">see how the implementation works</Link>.
        </p>
      </section>
    </article>
  );
}
