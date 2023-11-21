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
  FaXmark,
  FaBackward,
  FaPenToSquare,
  FaTrashCan
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

export function MetaButton({onClick, style, className, children, meta}) {
  const [metaVal] = useState(meta)

  const onClickHandler = (e) => {
    onClick(metaVal, e)
  }

  return (<button onClick={onClickHandler} style={style} className={className}>{children}</button>)
}

export function ListZones({ max }) {
  const [zones, setZones] = useState(null)
  const [current, setCurrent] = useState(null)
  const {getAuthHeader, getUrl } = useAppState()
  const [error, setError] = useState();
  const [isedit, setEditOpen] = useState(false)
  const [editZoneCaption, setEditZoneCaption] = useState("Add Zone")
  const [currentDirection, setCurrentDirection] = useState("forward")
  const [canMoveForward, setCanMoveForward] = useState(false)
  const [canMoveBackward, setCanMoveBackward] = useState(false)
  const [navKeys, setNavKeys] = useState(null)
  const initialized = useRef(false)

  const reload = async (from = null, direction="forward") => {

    setZones(null);
    setCurrentDirection(direction)
  
    try {
      let res = await fetch(getUrl('/zone' + qargs(from, max, null, direction)), {
        method: "get",
        headers: getAuthHeader()
      });

      if (res.ok) {
        const z = await res.json();
        console.log(`fetched: current="${current}" from=${from} direction=${direction} canFwd=${canMoveForward}, canBackw=${canMoveBackward} more=${z.more}: `, res);
        console.log(`fetched json: `, z);
        setZones(z.value);
        setNavKeys({kfirst: z.kfirst, klast: z.klast})

        if (direction == "forward") {
          setCanMoveBackward(from !== null)
          setCanMoveForward(z.more)
        } else {
          if (!z.more) {
            // We have moved back  to the start.
            setCurrent(null)
            setCanMoveBackward(false)  
            setCanMoveForward(zones.length === max)
          } else {
            setCanMoveBackward(true)
            setCanMoveForward(true)
          }
        }

      } else {
        setError(Error(`Failed to fetch zones: ${res.statusText}`))
      }
    } catch (err) {
      console.log(`Caught error: `, err)
      setError(err)
    }
  }


  const reloadCurrent = () => {
    reload(current, currentDirection);
  }

  const moveFirst = () => {
    setCurrent(null);
    setNavKeys(null)
    setCanMoveBackward(false)
    reload();
  }

  const moveNext = () => {
    if (zones && zones.length && navKeys) {
      const last = navKeys.klast
      setCurrent(last)
      reload(last)
    }
  }

  const movePrev = () => {
    if (zones && zones.length && navKeys) {
      const first = navKeys.kfirst
      setCurrent(first)
      reload(first, "backward")
    }
  }

  // Load once
  useEffect(() => {

    // Avoid double initialization by "React.StrictMode"
    if (initialized.current) {
      return
    }

    initialized.current = true;

    const savedCurrent = window.localStorage.getItem('zones.current')
    const savedDirection = window.localStorage.getItem('zones.direction')
    console.log('Saved current is: ', savedCurrent)

    let dir = null;
    if (savedDirection) {
      dir = savedDirection
      setCurrentDirection(savedDirection)
    }

    if (savedCurrent && savedCurrent.length) {
      console.log("if (savedCurrent): ", savedCurrent)
      setCurrent(savedCurrent)
      reload(savedCurrent, dir)
    } else {
      console.log('no saveCurrent! Calling pure reload()')
      reload();
    }
  }, []);

  useEffect(() => {
    console.log('saving current: ', current)
    window.localStorage.setItem('zones.current', current ? current : "")
  },[current])

  useEffect(() => {
    console.log('saving direction: ', currentDirection)
    window.localStorage.setItem('zones.direction', currentDirection)
  },[current])

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

  const deleteZone = async (zoneName) => {
    
    if (window.confirm(`Do you really want to delete the zone ${zoneName}?\r\nThis cannot be un-done.`)) {

      try {
      const res = await fetch(getUrl(`/zone/${zoneName}`), {
        method: "delete",
        headers: getAuthHeader()});

        console.log("fetch delete: ", res)

        if (res.ok) {
            // Todo - some OK effect
          reloadCurrent()
          return
        }

        throw Error(res.statusText)

      } catch(error) {
        console.log("Fatch failed: ", error)
        //throw new Error("Failed to validate authentication with server")
        setError("Request failed")

        if (error instanceof Error) {
            setError(error.message)
        }
      }
    }
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
              <td><Link to={`rr?z=${name}`} className='w3-button w3-blue w3-padding w3-round-large w3-tiny'><FaPenToSquare/> Manage</Link>
              <MetaButton meta={name} onClick={deleteZone} className='w3-button w3-red w3-padding w3-round-large w3-tiny' style={{ marginLeft: "1em" }}><FaTrashCan/> delete</MetaButton></td>
            </tr>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: "6px" }}>
        <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep /> From Start</button>
        <button className='w3-button w3-blue' onClick={movePrev} disabled={!canMoveBackward}><FaBackward /> Prev</button>
        <button className='w3-button w3-teal' onClick={reloadCurrent} ><FaRepeat /> Reload</button>
        <button className='w3-button w3-blue' onClick={moveNext} disabled={!canMoveForward}><FaForward /> Next</button>
        <button className='w3-button w3-green w3-padding w3-round-large w3-tiny' onClick={addZone} ><FaPlus /> Add Zone</button>
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
