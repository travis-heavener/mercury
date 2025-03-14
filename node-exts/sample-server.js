/**
 * A sample JS file implementing the Node Extension Driver for Mercury.
 */

import * as driver from "./node-ext-driver.js";

// Establish connection to Mercury server
const conn = driver.connect();

// Bind endpoint handlers
conn.get("/test", (req, res) => {
    res.json({ "ping": "pong" });
});