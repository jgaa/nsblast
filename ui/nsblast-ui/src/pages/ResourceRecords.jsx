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
  FaXmark
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

function EditRr(zone, name, caption, edit=true) {
    const {getUrl, getAuthHeader, setToken} = useAppState();
    const {close} = usePopupDialog();
    const [errorMsg, setError] = useState("");
    const [rr, setRr] = useState(null)
    const nameRef = useRef(name)

    const submit = async (e) => {
        e.preventDefault();
    
        const fqdn =  name.length ? `${name}.zone` : zone;
    
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

      const fetchExisting = () => {

      } 

      useEffect(() => {
        if (edit) {
            fetchExisting();
        } else {
            setRr({});
        }

      }, []);
    
      const onCancel = () => {
        console.log('EditZone: onCancel called.')
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
    
            <label>Name</label>
            <input ref={nameRef} className="w3-input" type="text" required disabled={edit}/>
    
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

    const reload = async (from = null) => {

        setRrs(null);
    
        try {
          let res = await fetch(getUrl(`/zone/${zone}` + qargs(from)), {
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
          const last = rrs.at(-1);
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
        console.log("editRr name=", name)
        setEditRrCaption(`Edit Resource ${name}`)
        setRrName(name)
        setEditOpen(true)
        setEditMode(true)
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
            <th>Name</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {rrs.map((name) => (
            <tr>
              <td>
                {name}
              </td>
              <td><button onClick={() => {editRr(name)}} className='w3-button w3-blue'>Edit</button> |delete</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: "6px" }}>
        <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep /> From Start</button>
        <button className='w3-button w3-green' onClick={reloadCurrent} ><FaRepeat /> Reload</button>
        <button className='w3-button w3-blue' onClick={moveNext} disabled={!hasMore}><FaForward /> Next</button>
        <button className='w3-button w3-orange' onClick={addRr} ><FaPlus /> Add</button>
      </div>
      <PopupDialog zone={{ fqdn: 'example.com' }}
        isOpen={isEditOpen}
        onClosed={onEditClosed}>
        {/* <EditRr zone={zone} name={rrName} caption={editRrCaption} edit={isEditMode}/> */}
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