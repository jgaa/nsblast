import React from 'react'

export default function ErrorScreen({ error }) {
    //
    // Here you can handle or track the error before rendering the message
    //
  
    return (
      <div className="w3-error">
        <h3>We are sorry... something went wrong</h3>
        <p>We cannot process your request at this moment.</p>
        <p>ERROR: {error.message}</p>
      </div>
    );
  }