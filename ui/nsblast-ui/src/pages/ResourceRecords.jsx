import React, { useState, useEffect, useRef } from 'react';
import { useAppState } from '../modules/AppState'
import ErrorBoundary from '../modules/ErrorBoundary';
import {
  FaBackwardStep,
  FaRepeat,
  FaForward,
  FaPlus,
  FaFloppyDisk,
  FaCross,
  FaXmark,
  FaTrashCan
} from "react-icons/fa6"
//import ErrorScreen from '../modules/ErrorScreen';
import { BeatLoader } from 'react-spinners';
import PopupDialog, { usePopupDialog } from '../modules/PopupDialog';
import {
    BrowserRouter as Router,
    Link,
    useLocation
  } from "react-router-dom";
import qargs from '../modules/qargs';
  
function useQuery() { 
    const { search } = useLocation();
  
    return React.useMemo(() => new URLSearchParams(search), [search]);
}

function getRrRows(rr) {
  let rows = 0
  for (const [key, value] of Object.entries(rr)) {
    if (key == 'ttl' || key == 'fqdn') {
      continue
    }
    if (Array.isArray(value)) {
      rows += value.length
    } else {
      rows += 1
    }
  }

  //return 2
  return rows ? rows : 1
}

function RrObject({name, value}) {
  return (
    <>
    {name}={value}<br/>
    </>
  )
}

function AddRrData({type, entry}) {

  console.log('type=', type, " entry=", entry)

  if (Array.isArray(entry)) {
      return (
        <>
        {entry.map((value) => (
          <AddRrData type={type} entry={value}/>
        ))}
        </>
      )
  }

  switch(type) {
    case 'ttl':
    case 'fqdn':
      return (<></>)
    case 'soa':
      return (
        <tr>
          <td>{type}</td>
          <td>email={entry.email}<br/>
              mname={entry.mname}<br/>
              expire={entry.expire}<br/>
              minimum={entry.minimum}<br/>
              refresh={entry.refresh}<br/>
              serial={entry.serial}
          </td>
          <td>edit|delete</td>
        </tr>
      )
    default:
      if (entry === Object(entry)) {
        return (
          <tr>
          <td>{type}</td>
          <td>
          {Object.entries(entry).map((array) => (
            <RrObject name={array[0]} value={array[1]} />
          ))}
          </td>
          <td>edit|delete</td>
          </tr>  
        )
      }

      return (
        <tr>
          <td>{type}</td>
          <td>{entry}</td>
          <td>edit|delete</td>
        </tr>
      )
    
      // default:
      //   return (<p>Undefined {type}</p>)
  }
}

function RrCells({rr}) {

  if (!rr) {
    return (<p>Empty</p>)
  }

  const rows = getRrRows(rr)
 
  return (
    <>
    <tr>
      <td rowSpan={rows + 2}>{rr.fqdn}</td>
    </tr>
      {Object.entries(rr).map((array) => (
        <AddRrData type={array[0]} entry={array[1]} />
      ))}
    <tr>
      <td colSpan="3">
      <button className="w3-button w3-red w3-padding" style={{ marginLeft: "1em" }}
              type="button"> <FaTrashCan/>Delete fqdn</button>
      <button className="w3-button w3-green w3-padding" style={{ marginLeft: "1em" }}
              type="button"> <FaPlus/>Add Resource Record</button>
      </td>
    </tr>
    </>
  )
}

function EditRr({zone, name, caption, edit}) {
    const {getUrl, getAuthHeader, setToken} = useAppState();
    const {close} = usePopupDialog();
    const [errorMsg, setError] = useState("");
    const [rr, setRr] = useState(null)
    const nameRef = useRef(name)

    console.log(`Entering EditRr. zone=${zone}, name=${name}, edit=${edit}`)

    const submit = async (e) => {
        e.preventDefault();
    
        const fqdn = name
    
        console.log(`EditRr submit zone ${fqdn} rr=${rr}: `, e)
    
        // let data = {soa: {
        //   email: emailRef.current.value
        // }};

        const data = {};
    
        // Todo validate valid fqdn
        try {
          const res = await fetch(getUrl(`/rr/${fqdn}`), {
              method: "put",
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
          setError("Request failed")
    
          if (error instanceof Error) {
              setError(error.message)
          }
        }
      }

      const fetchExisting = async () => {

        const fqdn = name

        console.log(`fetchExisting called for fqdn ${fqdn}`)

        try {
          let res = await fetch(getUrl(`/rr/${fqdn}`), {
            method: "get",
            headers: getAuthHeader()
          });
    
          if (res.ok) {
            console.log(`fetched rr res: `, res);
            let z = await res.json();
            console.log(`fetched rr json: `, z);
            setRr(z.value);
          } else {
            setError(Error(`Failed to fetch resource records for ${fqdn}: ${res.statusText}`))
          }
        } catch (err) {
          console.log(`Caught error: `, err)
          setError(err)
        }
      } 

      useEffect(() => {

        console.log(`EditRr: init useEffect: edit= ${edit} name=${name}`)
        if (edit) {
            fetchExisting();
        } else {
            setRr({});
        }

      }, []);
    
      const onCancel = () => {
        console.log('EditRr: onCancel called.')
        close();
      }

      if (!rr) return(<BeatLoader />);

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
    
            <label hidden={edit}>Name</label >
            <input ref={nameRef} className="w3-input" type={edit ? "hidden" : "text"} required={!edit}/>

            <br/>
            <RrCells rr={rr}/>
            <br/>
    
            <button className="w3-button w3-green w3-padding" type="submit" ><FaFloppyDisk /> Save</button>
            <button className="w3-button w3-gray w3-padding" style={{ marginLeft: "1em" }}
              type="button" onClick={onCancel}><FaXmark />Cancel</button>
          </form>
        </>
      )
}


function ListResourceRecords({ max, zone }) {
    const [rrs, setRrs] = useState(null)
    const [current, setCurrent] = useState(null)
    const {getAuthHeader, getUrl } = useAppState()
    const [error, setError] = useState();
    const [isEditOpen, setEditOpen] = useState(false)
    const [hasMore, setHasMore] = useState(false)
    const [editRrCaption, setEditRrCaption] = useState("Add Record")
    const [rrName, setRrName] = useState("")
    const [isEditMode, setEditMode] = useState(false)
    const [currentZone, _] = useState(zone)

    const reload = async (from = null) => {

        setRrs(null);
    
        try {
          let res = await fetch(getUrl(`/zone/${zone}` + qargs(from, max, 'verbose')), {
            method: "get",
            headers: getAuthHeader()
          });
    
          if (res.ok) {
            console.log(`fetched: `, res);
            let z = await res.json();
            console.log(`fetched json: `, z);
            setRrs(z.value);
            setHasMore(z.more)
          } else {
            setError(Error(`Failed to fetch rr's: ${res.statusText}`))
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
        if (rrs && rrs.length) {
          const last = rrs.at(-1).fqdn;
          reload(last)
          setCurrent(last)
        }
      }

      const addRr= () => {
        setEditRrCaption("Add Resource")
        setEditOpen(true)
      }

      const onEditClosed = () => {
        console.log('Edit rr dialog closed')
        setEditOpen(false)
        reloadCurrent()
      }
    
      // Load once
      useEffect(() => {
        reload();
      }, []);

      if (error) {
        console.log("Got error err: ", error)
        throw Error("Error!")
      }
    
    const editRr = (name) => {
        console.log(`editRr zone=${zone}, name=${name} =`)
        setEditRrCaption(`Edit Resource ${name}`)
        setRrName(name)
        setEditMode(true)
        setEditOpen(true)
    }

    if (!rrs) return (<BeatLoader />);


    return (
        <div className="w3-container w3-cell w3-mobile">
      <header className="w3-container w3-blue">
        <h4>fqdn's in this zone</h4>
      </header>
      <table className='w3-table-all'>
        <thead>
          <tr>
            <th>fqdn</th>
            <th>type</th>
            <th>value</th>
            <th>actions</th>
          </tr>
        </thead>
        <tbody>
          {rrs.map((entry, id) => (           
              <RrCells rr={entry}/>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: "6px" }}>
        <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep /> From Start</button>
        <button className='w3-button w3-yellow' onClick={reloadCurrent} ><FaRepeat /> Reload</button>
        <button className='w3-button w3-blue' onClick={moveNext} disabled={!hasMore}><FaForward /> Next</button>
        <button className='w3-button w3-green' onClick={addRr} ><FaPlus />Add fqdn</button>
      </div>
      <PopupDialog zone={{ fqdn: 'example.com' }}
        isOpen={isEditOpen}
        onClosed={onEditClosed}>
        <EditRr zone={zone} name={rrName} caption={editRrCaption} edit={isEditMode}/> 
      </PopupDialog>
    </div>
    )
}

export default function ResourceRecords() {
    const query = useQuery();
    const zone = query.get("z")

    return (
        <>
        <h2>Managing zone <span className='w3-blue'>{zone}</span></h2>
        <ErrorBoundary fallback={<p>Something went wrong</p>}>
            <ListResourceRecords zone={zone} max={10} />
        </ErrorBoundary>
        </>
    )
}