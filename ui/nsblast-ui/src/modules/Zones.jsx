import React, {useState, useEffect} from 'react';
import { useAppState } from './AppState'
import ErrorBoundary from './ErrorBoundary';

/* 
 v Create layout for table
 v Create layout for one item
 v Fetch one page
 v Update when data is available
 - add next button
 - allow user to browse forward
 - add prev button
 - allow user to browse backward
 - add buttons on items to edit a zone 
    - SOA record
    - Go to the RR list
 - add button to delete a zone
 - add button to change the zone information (soa)
 - add button to add a zone
   - dialog with basic info; SOA
*/

export function ListZones({max}) {
  
  const [zones, setZones] = useState(null)
  let { isLoggedIn, getAuthHeader, getUrl } = useAppState()
  const [error, setError] = useState();

  const reload = () => {
    fetch(getUrl(`/zone?limit=${max}`), {
      method: "get",
      headers: getAuthHeader()
      })
      .then(res => {
        if (res.ok) {
          console.log(`fetched: `, res);
          res.json().then(data => {
          console.log(`fetched json: `, data);
          setZones(data.value);
          });
        } else {
          setError(`Fetch failed ${res.status}: ${res.statusText}`)
        }
      })
      .catch(setError);
  }

  useEffect(() => {
    reload();
  }, []);



  if (error) {
    return (<h1>Failed with error</h1>)
  } 

  if (!zones) return <h1>loading...</h1>;;

  console.log("Zones when rendering: ", zones)
  
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
          {zones.map((name) => (
            <tr>
              <td>
                {name}
              </td>
              <td>zone</td>
              <td>-</td>
              <td>edit|rr|delete</td>
            </tr>
          ))}
        </tbody>
        </table>
        <div style={{marginTop:"6px"}}>
                  <button className='w3-button w3-green' onClick={reload} >Reload</button>
        </div>
    </div>
   );
}

export function Zones(props) {

  return (

    <ErrorBoundary fallback={<h1>Failed with error</h1>}>
      <ListZones max={10}/> 
    </ErrorBoundary>
  );
}
