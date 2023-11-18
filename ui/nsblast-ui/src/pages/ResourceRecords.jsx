import React, { useState, useEffect, useRef } from 'react';
import { useAppState } from '../modules/AppState'
import ErrorBoundary from '../modules/ErrorBoundary';
import {
  FaBackwardStep,
  FaRepeat,
  FaForward,
  FaPlus,
  FaFloppyDisk,
  FaPen,
  FaXmark,
  FaTrashCan
} from "react-icons/fa6"
import { BeatLoader } from 'react-spinners';
import PopupDialog, { usePopupDialog } from '../modules/PopupDialog';
import {
    BrowserRouter as Router,
    Link,
    useLocation
  } from "react-router-dom";
import qargs from '../modules/qargs';

const rrProto =
// Defines valid RR's and their format
 {
  soa: {
    brief: "Zone info",
    canAdd: false,
    proto: {
        mname: "",
        rname: "",
        email: "",
        refresh: 0,
        retry: 0,
        expire: 0,
        minimum: 0
      },
    },
  ns: {
    brief: "Name Server",
    canAdd: false,
    proto: [""],
    },
  a: {
    brief: "IP number v4 or v6",
    canAdd: true,
    proto: [""],
    },
  mx: {
    brief: "Mail Server info",
    canAdd: true,
    proto: [
      {
        host: "",
        priority: 0
      }
      ],
    },
  txt: {
    brief: "Text record",
    canAdd: true,
    proto: [""],
    },
  hinfo: {
    brief: "",
    canAdd: true,
    proto: {
      cpu: "",
      os: ""
      },
    },
  rp: {
    brief: "",
    canAdd: true,
    proto: {
      mbox: "",
      txt: ""
      },
    },
  afsdb: {
    brief: "",
    canAdd: true,
    proto: [
      {
        host: "",
        subtype: 0
      }
    ],
    },
  srv: {
    brief: "",
    canAdd: true,
    proto: [
      {
        target: "",
        priority: 0,
        weight: 0,
        port: 0
      }
    ],
    }
  }


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

  return rows ? rows + 2 : 2
}

function RrObject({name, value}) {
  return (
    <>
    {name}={value}<br/>
    </>
  )
}

function EditDeleteBtn({onDelete, onEdit}) {
  return (
    <>
      <button className="w3-button w3-yellow w3-padding w3-round-large w3-tiny" style={{ marginLeft: "1em" }}
    type="button" onClick={onEdit}> <FaPen/>Edit</button>
      <button className="w3-button w3-red w3-padding w3-round-large w3-tiny" style={{ marginLeft: "1em" }}
    type="button" onClick={onDelete}> <FaTrashCan/>Delete</button>
    </>
  )
}

function AddRrData({type, entry, deleteRr, editRr, ix=0}) {

  //console.log('type=', type, " entry=", entry)

  if (Array.isArray(entry)) {
      return (
        <>
        {entry.map((value, i) => (
          <AddRrData type={type} entry={value} deleteRr={deleteRr} editRr={editRr} ix={i}/>
        ))}
        </>
      )
  }

  const onDelete = () => {
    deleteRr(type, ix)
  }

  const onEdit = () => {
    editRr(type, ix)
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
          <td><EditDeleteBtn onDelete={onDelete} onEdit={onEdit}/></td>
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
          <td><EditDeleteBtn onDelete={onDelete} onEdit={onEdit}/></td>
          </tr>  
        )
      }

      return (
        <tr>
          <td>{type}</td>
          <td>{entry}</td>
          <td><EditDeleteBtn onDelete={onDelete} onEdit={onEdit}/></td>
        </tr>
      )
    
      // default:
      //   return (<p>Undefined {type}</p>)
  }
}

function RrCells({rr, onChange, onEdit, onAdd, rrIx}) {
  const {getUrl, getAuthHeader} = useAppState();

  if (!rr) {
    return (<p>Empty</p>)
  }

  const rows = getRrRows(rr)

  const deleteFqdn = async () => {
    if (window.confirm(`Do you really want to delete fqdn ${rr.fqdn} ?`)) {
      console.log(`deleting fqdn ${rr.fqdn}`)

      try {
        const res = await fetch(getUrl(`/rr/${rr.fqdn}`), {
            method: "delete",
            headers: {...getAuthHeader()}
            });

        console.log("fetch delete res: ", res)

        if (!res.ok) {
          throw Error(res.statusText)
        }
        onChange()

    } catch(error) {
        console.log("Delete failed: ", error)
        window.alert(`Deletion of fqdn ${rr.fqdn} failed\n\r${error.message}`)
      }
    }
  }

  const editRr = async (type, ix=0) => {
    console.log(`edit rr at #${rrIx} ${rr.fqdn} type=${type}, ix=${ix}`)

    // rrIx, fqdn, type, ix = 0
    onEdit(rrIx, rr.fqdn, type, ix)
  }

  const deleteRr = async (type, ix=0) => {
    let rr_obj = {...rr}
    delete rr_obj.ttl
    delete rr_obj.fqdn

    // We should not send both rname and email.
    if ('soa' in rr_obj) {
      delete rr_obj.soa.rname
    }

    let found = false

    console.log(`rr_obj is ${rr_obj}: `, rr_obj)

    for (const [key, value] of Object.entries(rr_obj)) {
      if (key === type) {
        if (Array.isArray(value) && value.length > 1) {
          rr_obj[key].splice(ix, 1)
        } else {
          delete rr_obj[key]
        }

        found = true
        break;
      }
    }

    if (!found) {
      console.log(`The rr item was not found!`)
      return
    }

    if (window.confirm(`Do you really want to delete this "${type}" Resorce Record?`)) {
      console.log(`deleting rr type=${type}, ix=${ix}, new rr: `, rr_obj)

      try {
        const res = await fetch(getUrl(`/rr/${rr.fqdn}`), {
          method: "put",
          headers: {...getAuthHeader(), 
            'Content-Type':'application/json'},
          body: JSON.stringify(rr_obj)
          });

        console.log("fetch delete rr res: ", res)

        if (!res.ok) {
          throw Error(res.statusText)
        }
        onChange()

    } catch(error) {
        console.log("Delete failed: ", error)
        window.alert(`Deletion of fqdn ${rr.fqdn} failed\n\r${error.message}`)
      }
    }
  }

  const addRr = () => {
    onAdd(rrIx, rr.fqdn)
  }
 
  return (
    <>
    <tr>
      <td rowSpan={rows}>{rr.fqdn}</td>
    </tr>
      {Object.entries(rr).map((array, ix) => (
        <AddRrData type={array[0]} entry={array[1]} deleteRr={deleteRr} editRr={editRr}/>
      ))}
    <tr>
      <td colSpan="3">
      <button className="w3-button w3-red w3-padding w3-round-large w3-tiny" style={{ marginLeft: "1em" }}
              type="button" onClick={deleteFqdn}> <FaTrashCan/> Delete fqdn</button>
      <button className="w3-button w3-green w3-padding w3-round-large w3-tiny" style={{ marginLeft: "1em" }}
              type="button" onClick={addRr}> <FaPlus/> Add Resource Record</button>
      </td>
    </tr>
    </>
  )
}

function AddFqdn({args}) {
    const {getUrl, getAuthHeader, setToken} = useAppState();
    const {close} = usePopupDialog();
    const [errorMsg, setError] = useState("");
    const nameRef = useRef()

    const zone = args.zone
    const caption = args.caption

    console.log(`Entering AddFqdn. zone=${zone}`)

    const submit = async (e) => {
        e.preventDefault();
  
        const fqdn = `${nameRef.current.value}.${zone}`
    
        try {
          const res = await fetch(getUrl(`/rr/${fqdn}`), {
              method: "post",
              headers: {...getAuthHeader(), 
                'Content-Type':'application/json'},
              body: '{}'
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
    
      const onCancel = () => {
        console.log('EditRr: onCancel called.')
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
    
            <label>New name</label >
            <div><input ref={nameRef} className="w3-input" type="text" required="true" /><span>.{zone}</span></div>
    
            <button className="w3-button w3-green w3-padding" type="submit" ><FaFloppyDisk /> Save</button>
            <button className="w3-button w3-gray w3-padding" style={{ marginLeft: "1em" }}
              type="button" onClick={onCancel}><FaXmark />Cancel</button>
          </form>
        </>
      )
}

function RrInputText({name, value, registerRef}) {
  const valueRef = useRef(value)

  useEffect(() => {
    valueRef.current.value = value
    registerRef(name, valueRef)
  }, []);

  return (
    <tr>
    <td>
    <label>{name}</label >
    </td>
    <td>
    <input ref={valueRef} className="w3-input" type="text" required="true" />
    </td>
    </tr>
  )
}

function InputTable({children}) {
  return (
    <table style={{width:"100%"}}>
      <tbody>
        {children}
      </tbody>
    </table>
  )
}

function RrInputs({args, registerRef, proto=null}) {
  // Take the object we want to edit from 'rr'
  
  let val = proto ? proto : args.rr[args.type]
  
  if (Array.isArray(val)) {
    val = val[proto ? 0 : args.ix]
  }

  // 'args.type' is the DNS resource type we are editing
  // 'val' is the value. It is a string or an object

  if (val === Object(val)) {
    // Create a table with the items
    
    if (args.type == 'soa') {
      delete val.serial
      delete val.rname
    }

    return (
      <InputTable>
      {Object.entries(val).map((a, ix) => (
        <RrInputText name={a[0]} value={a[1]} registerRef={registerRef}/>
      ))}
      </InputTable>
    )
  }

  console.log(`RrInputs val: `, val)

  return (
    <InputTable>
    <RrInputText name="value" value={val} registerRef={registerRef}/>
    </InputTable>
  )

}

// Example args...
// args = {
//   "mode": "editRr",
//   "zone": "a-example.com",
//   "fqdn": "all.a-example.com",
//   "type": "a",
//   "ix": 1,
//   "caption": "Edit Resource Record",
//   "rr": {
//     "fqdn": "all.a-example.com",
//     "ttl": 2592000,
//     "a": [
//       "127.0.0.1",
//       "127.0.0.2"
//     ],
//     "mx": {
//       "host": "mail.nsblast.com",
//       "priority": 10
//     },
//     "txt": [
//       "Dogs are cool",
//       "My dogs are the best ones"
//     ],
//     "hinfo": {
//       "cpu": "Asome",
//       "os": "Linux"
//     },
//     "rp": {
//       "mbox": "teste",
//       "txt": 9
//     },
//     "afsdb": {
//       "host": "asfdb.nsblast.com",
//       "subtype": 1
//     },
//     "srv": {
//       "target": "a-example.com",
//       "priority": 1,
//       "weight": 2,
//       "port": 22
//     },
//     "dhcid": "YmE4ODM1ZWMtODM5Zi0xMWVlLTgzNWYtYmZhN2ZlYzJmYWQwCg==",
//     "openpgpkey": "YzE1NWU2M2EtODM5Zi0xMWVlLTk0YWQtYzdkMjM3ZWJmNTg4Cg==",
//     "#42": [
//       "YjZkYTYzZDQtODM5MC0xMWVlLWE4NGEtZjcxZjAxZDY4NTdlCg=="
//     ],
//     "#142": [
//       "ZGU2NDYwZWUtODM5MC0xMWVlLTkxZmQtNmI1Njc1ZGRkOTFkCg=="
//     ]
//   }
// }

function castStringToType(type, newVar) {

  const nvt = typeof newVar
  if (typeof newVar != 'string') {
    throw Error(`castStringToType: newVar must be a string! It is a ${nvt}`)
  }
  
  switch (type) {
    case 'string':
      return newVar
    case 'number':
      return Number(newVar)
    case 'boolean':
      return newVar == 1 || newVar == 'true'
    default:
      throw Error(`castStringToType: Unerxpected type ${type}`)
  }
}

function setValueInRr(rr, type, ix, value) {

  const isAdding = ix === -1
  const destType = Object.hasOwn(rr, type) ? rr[type] : rrProto[type].proto

  console.log(`setValueInRr type=${type} ix=${ix} value=${value} isAdding=${isAdding} rr=`, rr, 'dstType=', destType)

  if (Array.isArray(destType)) {
    if (isAdding) {
      // add
      if (!Object.hasOwn(rr, type)) {
        rr[type] = []
      } 
      
      rr[type].push(value)
    } else {
      rr[type][ix] = value
    }
  } else {
    if (isAdding && Object.hasOwn(rr, type)) {
      throw Error(`There is already a resource-type ${type} in this fqdn.`)
    } else {
      rr[type] = value
    }
  }

  return rr
}

function prepareRrForUpdate(rr) {
  delete rr.fqdn
  return rr
}

function SelectRr({onSelectionChanged, enable, filter}) {
  const [all] =  useState(rrProto)

  if (!enable) {
    return (<></>);
  }

  let types = Object.keys(all).filter((name) => (all[name].canAdd)).filter((name) => (!filter.includes(name)))
  types.sort();

  const onChange = (event) => {

    let name = ""
    let proto = {}

    const selection = event.target.value

    if (selection !== "select") {
        proto = all[selection].proto
        name = selection
    }

    console.log(`SelectRr selextion=${selection} proto=`, proto)

    onSelectionChanged(name, proto)
  }

  return (
    <div class="w3-row w3-section">
    <div class="w3-col" style={{width:"10em"}}>Resource type</div>
    <div class="w3-rest">
    <select className='w3-select w3-border' onChange={onChange}>
      <option value="select"> -- Select a Resource type -- </option>

    {types.map((name) => (
      <option value={name}>{name} {all[name].brief}</option>
    ))}
    </select>
    </div>
    </div>
  )
}

// Some rr types can only occur once (non arrays)
// Prevent us from allowing the user to select such a type from the current rr
// if it is already used.
function filterUsedRrTypes(rr) {
  let filter = []

  const names = Object.keys(rr)
  names.map((name) => {
    const isArray = Array.isArray(rr[name])
    console.log(`fitering: name=${name}, rr=`, rr, ' is array=', isArray)
    if (!isArray) {
      filter.push(name)
    }
  })

  console.log(`filterUsedRrTypes: filter=`, filter, ' rr:', rr)

  return filter;
}

function AddRr({args}) {
  const isEditing = args.mode === 'editRr'
  const {getUrl, getAuthHeader, setToken} = useAppState();
  const {close} = usePopupDialog();
  const [formRefs, setFormRefs] = useState({})
  const [proto, setProto] = useState({})
  const [inputIsValid, setInputIsValid] = useState(isEditing)
  const [rrType, setRrType] = useState(isEditing ? args.type : "")
  const [filteredRrs, setFilteredRrs] = useState(filterUsedRrTypes(args.rr))

  console.log(`AddRr args: `, args)

  // Let the individual inputs register their refs here
  // We will collect the updated values on submit
  const registerRef = (name, ref) => {

    console.log('Registering ref: ', name, ' ', ref)

    let refs = formRefs;
    refs[name] = ref

    setFormRefs(formRefs)
  }

  const onSelectinChanged = (name, selectedProto) => {
    let cleared = formRefs
    const keys = Object.keys(cleared)
    keys.map((key) => {
      if (Object.hasOwn(cleared[key], 'current') && cleared[key].current !== null) {
        cleared[key].current.value = ""
      }
    })
    setFormRefs(cleared)

    console.log(`onSelectinChanged: selectedProto for ${name} is `, selectedProto)
    setProto(selectedProto)

    setInputIsValid(name !== '')
    setRrType(name)
  }

  const submit = async (e) => {
    e.preventDefault();

    let orig = isEditing ? args.rr[rrType] : proto 

    // If we are using a type that is not in the current orig (used to determine the new values type)
    // use the RR's prototype.
    if (!Object.hasOwn(orig, rrType)) {
      console.log(`submit: !Object.hasOwn(${rrType}) orig=`, orig)
      orig = rrProto[rrType].proto
    }

    console.log(`submit: isEditing=${isEditing}, rrType=${rrType} orig=`, orig, ' proto=', proto)

    let edited_value = {}

    // Collect the new value(s) form registered refs
    Object.entries(formRefs).map((a, ix) => {
      const name = a[0]
      const value = a[1].current.value

      if (name == 'value') {
        // Single value entry
        edited_value = value
        return
      }

      // What is the original type?
      let orgType = null
      if (Array.isArray(orig)) {
        orgType = typeof orig[0][name]
      } else {
        orgType = typeof orig[name]
      }

      console.log(`submit: orgType=${orgType} type=${rrType} name=${name} value=${value}`)

      // Build an object
      edited_value[name] = castStringToType(orgType, value)
    })

    console.log(`edited_value: `, edited_value)

    // Now, update the rr with the edited entry
    const ix = isEditing ? args.ix : -1
    const new_rr = prepareRrForUpdate(setValueInRr(args.rr, rrType, ix, edited_value))
    console.log(`new_rr: `, new_rr)

    const fqdn = args.fqdn

    try {
      const res = await fetch(getUrl(`/rr/${fqdn}`), {
          method: "put",
          headers: {...getAuthHeader(), 
            'Content-Type':'application/json'},
          body: JSON.stringify(new_rr)
          });

      console.log("fetch res: ", res)

      if (res.ok) {
          close()
      } else {
        throw Error(res.statusText)
      }

  } catch(error) {
      console.log("Fatch failed: ", error)
      window.alert(`Failed to save the updated record for fqdn ${fqdn}\n\r${error.message}`)
    }
  }

  const onCancel = () => {
    console.log('EditRr: onCancel called.')
    close();
  }

  return (
    <>
      <div className="w3-container w3-blue">
        <h2>{args.caption}</h2>
      </div>
      <form className="w3-container" onSubmit={submit}>

        <RrInputs args={args} proto={isEditing ? null : proto} registerRef={registerRef}/>
        <SelectRr onSelectionChanged={onSelectinChanged} enable={!isEditing} filter={filteredRrs}/>
        <button className="w3-button w3-green w3-padding" type="submit" disabled={!inputIsValid} ><FaFloppyDisk /> Save</button>
        <button className="w3-button w3-gray w3-padding" style={{ marginLeft: "1em" }}
          type="button" onClick={onCancel}><FaXmark />Cancel</button>
      </form>
    </>
  )
}


function AddPopup({args}) {

  switch(args.mode) {
    case 'addFqdn':
      return (<AddFqdn args={args}/>)
    case 'editRr':
      return (<AddRr args={args}/>) 
    case 'addRr':
      return (<AddRr args={args}/>) 
    default:
      throw Error(`AddPopup: Unknown mode ${args.mode}`)
  }
}

function ListResourceRecords({ max, zone }) {
    const [rrs, setRrs] = useState(null)
    const [current, setCurrent] = useState(null)
    const {getAuthHeader, getUrl } = useAppState()
    const [error, setError] = useState();
    const [isEditOpen, setEditOpen] = useState(false)
    const [hasMore, setHasMore] = useState(false)
    const [dlgArgs, setDlgArgs] = useState({})

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

      const addFqdn= () => {
        setDlgArgs({mode: 'addFqdn', zone:zone, caption: 'Add fqdn'})
        setEditOpen(true)
      }

      const editRr= (rrIx, fqdn, type, ix = 0) => {
        setDlgArgs({mode: 'editRr', zone:zone, fqdn:fqdn, type:type, ix:ix, caption: 'Edit Resource Record', rr:rrs[rrIx]})
        setEditOpen(true)
      }

      const addRr = (rrIx, fqdn) => {
        setDlgArgs({mode: 'addRr', zone:zone, fqdn:fqdn, caption: 'Add Resource Record', rr:rrs[rrIx]})
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
          {rrs.map((entry, ix) => (           
              <RrCells rr={entry} onChange={reloadCurrent} onEdit={editRr} onAdd={addRr} rrIx={ix}/>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: "6px" }}>
        <button className='w3-button w3-blue' onClick={moveFirst} ><FaBackwardStep /> From Start</button>
        <button className='w3-button w3-yellow' onClick={reloadCurrent} ><FaRepeat /> Reload</button>
        <button className='w3-button w3-blue' onClick={moveNext} disabled={!hasMore}><FaForward /> Next</button>
        <button className='w3-button w3-green w3-round-large w3-tiny' onClick={addFqdn} style={{ marginLeft: "1em" }}><FaPlus />Add fqdn</button>
      </div>
      <PopupDialog zone={{ fqdn: 'example.com' }}
        isOpen={isEditOpen}
        onClosed={onEditClosed}>
        <AddPopup args={dlgArgs}/> 
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