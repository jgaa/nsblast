type NotImplementedPageProps = {
  title: string;
  description?: string;
};

export default function NotImplementedPage({ title, description }: NotImplementedPageProps) {
  return (
    <div className="w3-panel w3-card w3-white" style={{ margin: '1rem' }}>
      <h2>{title}</h2>
      <p>{description ?? 'This feature is not implemented yet because backend support is missing.'}</p>
    </div>
  );
}
