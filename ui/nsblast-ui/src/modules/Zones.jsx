import React, {useState, useEffect} from 'react';
import { useAppState } from './AppState'
import ErrorBoundary from './ErrorBoundary';
import {
  FaBackwardStep,
  FaRepeat,
  FaForward
} from "react-icons/fa6"
import ErrorScreen from './ErrorScreen';
import { BeatLoader} from 'react-spinners';


/* 
 v Create layout for table
 v Create layout for one item
 v Fetch one page
 v Update when data is available
 v add next button
 v allow user to browse forward
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
  const [current, setCurrent] = useState(null) 
  let { isLoggedIn, getAuthHeader, getUrl } = useAppState()
  const [error, setError] = useState();

  const qargs = (from) => {
    let q = []
    if (max > 0) q.push(`limit=${max}`);
    if (from) q.push(`from=${from}`)
    if (q.length === 0)
      return "";
    return "?" + q.join('&')
  }

  const reload = async (from=null) => {

    setZones(null);

    try {
      let res = await fetch(getUrl('/zone' + qargs(from)), {
        method: "get",
        headers: getAuthHeader()
      });

      if (res.ok) {
        console.log(`fetched: `, res);
        let z = await res.json();
        console.log(`fetched json: `, z);
        setZones(z.value);
      } else {
        setError(Error(`Failed to fetch zones: ${res.statusText}`))
      }
    } catch (err) {
      console.log(`Caught error: `, err)
      setError(err)
    }
  } 


  const reloadCurrent = () => {
    reload(current);
  }

  const moveFirst = () => {
    setCurrent(null);
    reload();
  }

  const moveNext = () => {
      if (zones && zones.length) {
        const last = zones.at(-1);
        reload(last)
        setCurrent(last)
      }
  }

  // Load once
  useEffect(() => {
    reload();
  }, []);



  if (error) {
    console.log("Got error err: ", error)
    throw Error("Error!")
    //throw Error(error.message);
    //return (<p>{error.message}</p>)
  } 

  if (!zones) return (<BeatLoader/>);

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
            <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep/> From Start</button>
            <button className='w3-button w3-green' onClick={reloadCurrent} ><FaRepeat/> Reload</button>
            <button className='w3-button w3-blue' onClick={moveNext} ><FaForward/> Next</button>
        </div>
    </div>
   );
}

export function Zones(props) {

  return (

    <ErrorBoundary fallback={<p>Something went wrong</p>}>
      <ListZones max={10}/> 
    </ErrorBoundary>
  );
}
