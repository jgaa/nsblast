import React, { useState, useEffect, useRef } from 'react';
import { useAppState } from './AppState'
import ErrorBoundary from './ErrorBoundary';
import {
  FaBackwardStep,
  FaRepeat,
  FaForward,
  FaPlus,
  FaFloppyDisk,
  FaCross,
  FaXmark
} from "react-icons/fa6"
import ErrorScreen from './ErrorScreen';
import { BeatLoader } from 'react-spinners';
import PopupDialog, { usePopupDialog } from './PopupDialog';
import { Link } from "react-router-dom";
import qargs from './qargs';


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

const defaultZone = {
  fqdn: "",
  email: "",
  refresh: 0,
  retry: 0,
  expire: 0,
  minimum: 0
}

function EditZone({ zone, caption }) {
  let z = zone ? zone : defaultZone;
  const {getUrl, getAuthHeader, setToken} = useAppState();
  const {close} = usePopupDialog();

  
  const [errorMsg, setError] = useState("");

  const fqdnRef = useRef(z.fqdn)
  const emailRef = useRef(z.email)


  const submit = async (e) => {
    e.preventDefault();

    z.fqdn = fqdnRef.current.value;
    z.email = emailRef.current.value;

    console.log(`EditZone submit zone ${z.fqdn}: `, e)

    if (fqdnRef.current.value == "") {
      throw Error("Missing fqdn!")
    }

    let data = {soa: {
      email: emailRef.current.value
    }};

    // Todo validate valid fqdn
    try {
      const res = await fetch(getUrl(`/zone/${z.fqdn}`), {
          method: "post",
          headers: {...getAuthHeader(), 
            'Content-Type':'application/json'},
          body: JSON.stringify(data)
          });

      console.log("fetch res: ", res)

      if (res.ok) {
          // Todo - some OK effect
          close()
      }

  } catch(error) {
      console.log("Fatch failed: ", error)
      //throw new Error("Failed to validate authentication with server")
      setError("Request failed")

      if (error instanceof Error) {
          setError(error.message)
      }
    }
  }

  const onCancel = () => {
    console.log('EditZone: onCancel called.')
    close();
  }

  if (errorMsg.length > 0) {
    return (
      <>
        <div className="w3-container w3-blue">
          <h2>{caption}</h2>
        </div>
        <form className="w3-container" onSubmit={submit}>

          <div className="w3-row" style={{ marginLeft: "20%" }}>
            <div className=' w3-red'>
              <h3>Failed to send to server</h3>
              <p >{errorMsg}</p>
            </div>
          </div>

          <button className="w3-button w3-green w3-padding" type="submit" ><FaFloppyDisk /> Save</button>
          <button className="w3-button w3-gray w3-padding" style={{ marginLeft: "1em" }}
            type="button" onClick={onCancel}><FaXmark />Cancel</button>
        </form>
      </>
    )
  }

  return (
    <>
      <div className="w3-container w3-blue">
        <h2>{caption}</h2>
      </div>
      <form className="w3-container" onSubmit={submit}>

        <label>Fully Qualified Domain Name (fqdn)</label>
        <input ref={fqdnRef} className="w3-input" type="text" required />
        <label>Email to responsible person</label>
        <input ref={emailRef} className="w3-input" type="text" required />

        <button className="w3-button w3-green w3-padding" type="submit" ><FaFloppyDisk /> Save</button>
        <button className="w3-button w3-gray w3-padding" style={{ marginLeft: "1em" }}
          type="button" onClick={onCancel}><FaXmark />Cancel</button>
      </form>
    </>
  )
}

export function ListZones({ max }) {

  const [zones, setZones] = useState(null)
  const [current, setCurrent] = useState(null)
  const { getAuthHeader, getUrl } = useAppState()
  const [error, setError] = useState();
  const [isedit, setEditOpen] = useState(false)
  const [editZoneCaption, setEditZoneCaption] = useState("Add Zone")
  const [hasMore, setHasMore] = useState(false)

  // const qargs = (from) => {
  //   let q = []
  //   if (max > 0) q.push(`limit=${max}`);
  //   if (from) q.push(`from=${from}`)
  //   if (q.length === 0)
  //     return "";
  //   return "?" + q.join('&')
  // }

  const reload = async (from = null) => {

    setZones(null);

    try {
      let res = await fetch(getUrl('/zone' + qargs(from, max)), {
        method: "get",
        headers: getAuthHeader()
      });

      if (res.ok) {
        console.log(`fetched: `, res);
        let z = await res.json();
        console.log(`fetched json: `, z);
        setZones(z.value);
        setHasMore(z.more)
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

  const openEdit = () => {
    setEditOpen(true)
  }

  const addZone = () => {
    setEditZoneCaption("Add Zone")
    setEditOpen(true)
  }

  const onEditClosed = () => {
    console.log('Edit Zone dialog closed')
    setEditOpen(false)
    reloadCurrent()
  }


  if (error) {
    console.log("Got error err: ", error)
    throw Error("Error!")
  }

  if (!zones) return (<BeatLoader />);

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
          {zones.map((name, id) => (
            <tr key={id}>
              <td>
                {name}
              </td>
              <td>zone</td>
              <td>-</td>
              <td><Link to={`rr?z=${name}`} className='w3-button w3-blue'>Manage</Link> |delete</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: "6px" }}>
        <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep /> From Start</button>
        <button className='w3-button w3-green' onClick={reloadCurrent} ><FaRepeat /> Reload</button>
        <button className='w3-button w3-blue' onClick={moveNext} disabled={!hasMore}><FaForward /> Next</button>
        <button className='w3-button w3-orange' onClick={addZone} ><FaPlus /> Add</button>
      </div>
      <PopupDialog zone={{ fqdn: 'example.com' }}
        isOpen={isedit}
        onClosed={onEditClosed}>
        <EditZone caption={editZoneCaption} />
      </PopupDialog>
    </div>
  );
}

export function Zones(props) {

  return (

    <ErrorBoundary fallback={<p>Something went wrong</p>}>
      <ListZones max={10} >
      </ListZones>
    </ErrorBoundary>
  );
}
