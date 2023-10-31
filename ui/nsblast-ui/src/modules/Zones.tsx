import * as React from 'react';

export interface IZonesProps {
}

export function Zones(props: IZonesProps) {
  return (
    <div className="w3-container w3-cell w3-mobile">
      <header className="w3-container w3-blue">
        <h4>Zones</h4>
      </header>
      <table className='w3-table-all'>
        <thead>
          <tr>
            <th>Name</th>
            <th>Type</th>
            <th>RR's</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>example.com</td>
            <td>zone</td>
            <td>100</td>
            <td>edit|delete</td>
          </tr>
          <tr>
            <td>lastviking.eu</td>
            <td>zone</td>
            <td>8</td>
            <td>edit|delete</td>
          </tr>
        </tbody>
      </table>
    </div>
  );
}
