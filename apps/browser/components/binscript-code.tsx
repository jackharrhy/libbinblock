interface BinScriptCodeProps {
  html: string;
  label?: string;
  className?: string;
}

export function BinScriptCode({ html, label = 'BinScript source', className = '' }: BinScriptCodeProps) {
  return <div className={`binscript-code ${className}`.trim()} aria-label={label} dangerouslySetInnerHTML={{ __html: html }} />;
}
