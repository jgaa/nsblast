import * as React from 'react';

export interface ITenantStatsProps {
}

export default function TenantStats (props: ITenantStatsProps) {
  return (
    <div className="w3-container w3-cell w3-mobile">

    <header className="w3-container w3-blue">
      <h4>Stats</h4>
    </header>
    
    <table className="w3-table w3-border">
    <tbody>
    <tr>
        <td>Domains</td>
        <td>10</td>
        <td>Resource Records</td>
        <td>42</td>
    </tr>
    <tr>
        <td>Users</td>
        <td>3</td>
        <td>Lookups 24h</td>
        <td>88654</td>
    </tr>
    </tbody>
    </table>
    </div> 
  );
}
