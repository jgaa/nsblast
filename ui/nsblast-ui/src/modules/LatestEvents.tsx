import * as React from 'react';

export interface ILatestEventsProps {
}

export default function LatestEvents (props: ILatestEventsProps) {
  return (
    <div className="w3-container w3-cell w3-mobile">
    <header className="w3-container w3-blue">
      <h4>Latest Events</h4>
    </header>

    <ul className='w3-ul'>
        <li>Added zone example.com with many resource records</li>
        <li>Added user John Conner</li>
        <li>Deleted API key 'tmp'</li>
    </ul>
    </div> 
  );
}
