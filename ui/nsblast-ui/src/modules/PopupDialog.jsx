import React, { useState, useEffect, useRef, createContext, useContext } from 'react';

const PopupDialogContext = createContext();

export default function PopupDialog({ children, isOpen, onClosed }) {

    // const z = zone ? zone : defaultZone;

    // const [fqdn, setFqdn] = useState(z.fqdn)
    // const [email, setEmail] = useState(z.email)
    // const [refrest, setRefresh] = useState(z.refresh)
    // const [retry, setRetry] = useState(z.retry)

    const modalRef = useRef(null);
    const [isModalOpen, setModalOpen] = useState(isOpen);

    //console.log('In EditZone: props=', props)

    const handleClosed = () => {
        //console.log('onClosed is a:', onClosed)
        if (onClosed)
            onClosed();
        setModalOpen(false);
    }

    const handleKeyDown = (event) => {
        if (event.key === "Escape") {
            handleClosed();
        }
      };

    useEffect(() => {
        setModalOpen(isOpen);
    }, [isOpen]);


    useEffect(() => {
        const modalElement = modalRef.current;
        if (modalElement) {
            if (isModalOpen) {
                modalElement.showModal();
            } else {
                modalElement.close();
            }
        }
    }, [isModalOpen]);

    // Exported via context
    const close = () => {
        console.log('PopupDialog: close called!')
        handleClosed()
    }

    return (
        <dialog ref={modalRef} 
            onKeyDown={handleKeyDown}
            className="w3-modal-" >
            <div className="w3-modal-content">
                <div className="w3-container">
                <PopupDialogContext.Provider value={{close}}>
                    {children}
                </PopupDialogContext.Provider>
                </div>
                {/* <button className="w3-button w3-grey" 
                    onClick={handleClosed}>
                    Cancel
                </button> */}
            </div>
        </dialog>
    );

    };

    export const usePopupDialog = () => useContext(PopupDialogContext);