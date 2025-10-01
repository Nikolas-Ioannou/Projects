const express = require('express');
const path = require('path');
const bodyParser = require('body-parser');
const mysql = require('mysql2');
const bcrypt = require('bcrypt');

const app = express();
const port = 3000;
const session = require('express-session');
app.use(bodyParser.urlencoded({ extended: false }));
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

const db = mysql.createConnection({ //Change these info if needed
  host: 'localhost',
  user: 'myuser',
  password: '', 
  database: 'registration_app'
});

app.use(session({
  secret: 'username', 
  resave: false,
  saveUninitialized: true,
}));

db.connect(err => {
  if (err) console.error(err);
  else {
    console.log('Connected to MySQL');
  }
});

// =============== ROUTES  ===============
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'public', 'welcome.html')));
app.get('/register', (req, res) => res.sendFile(path.join(__dirname, 'public', 'register.html')));
app.get('/admin.html', (req, res) => res.sendFile(path.join(__dirname, 'public', 'admin.html')));
app.get('/hub.html', (req, res) => { res.sendFile(path.join(__dirname, 'public', 'hub.html')); });
app.get('/manage.html', (req, res) => { res.sendFile(path.join(__dirname, 'public', 'manage.html')); });
app.get('/browse.html', (req, res) => { res.sendFile(path.join(__dirname, 'public', 'browse.html')); });

// =============== REGISTER PAGE ===============
app.post('/register', async (req, res) => {
  const { username, password, passwordConfirm, firstname, lastname, email, phone, address, location, afm } = req.body;
  if (password !== passwordConfirm) 
    return res.redirect('/register?error=Οι κωδικοί δεν ταιριάζουν');
  try {
    const hashed = await bcrypt.hash(password, 10);
    const sql = `INSERT INTO users (username,password,first_name,last_name,email,phone,address,location,afm,approved,is_admin)
      VALUES (?,?,?,?,?,?,?,?,?,FALSE,FALSE)`;
    db.query(sql, [username, hashed, firstname, lastname, email, phone, address, location, afm], (err) => {
      if (err) {
        if (err.code === 'ER_DUP_ENTRY') return res.redirect('/register?error=Το όνομα χρήστη υπάρχει');
        return res.redirect('/register?error=Σφάλμα στη βάσης δεδομένων');
      }
      res.redirect('/?success=Η εγγραφή ολοκληρώθηκε. Αναμονή για έγκριση.');
    });
  } catch (err) { 
    console.error(err); 
    res.redirect('/register?error=Σφάλμα διακομιστή'); 
  }
});

// =============== LOG IN PAGE ===============
app.post('/login', (req, res) => {
  const { username, password } = req.body;
  db.query(`SELECT * FROM users WHERE username=?`, [username], (err, results) => {
    if (err) return res.redirect('/?error=Σφάλμα στη βάση δεδομένων');
    if (results.length === 0) return res.redirect('/?error=Μη έγκυρο όνομα χρήστη ή κωδικός');
    const user = results[0];
    if (!user.approved) return res.redirect('/?error=Ο λογαριασμός δεν έχει εγκριθεί');
    bcrypt.compare(password, user.password, (err, match) => {
      if (match) {
        req.session.username = user.username; // Store the username which is currently logged in
        if (user.is_admin) {
          res.redirect('/admin.html');
        } else {
          res.redirect('/hub.html'); 
        }
      } else {
        res.redirect('/?error=Μη έγκυρο όνομα χρήστη ή κωδικός');
      }
    });
  });
});


// =============== ADMIN PAGE  ===============

// Pending users 
app.get('/api/pending-users', (req, res) => {
  let { page = 1, pageSize = 10 } = req.query;
  page = Number(page);
  pageSize = Number(pageSize);
  db.query('SELECT COUNT(*) AS total FROM users WHERE approved=FALSE', (err, countResult) => {
    if (err) return res.json({ users: [], totalPages: 1, currentPage: page });
    const totalUsers = countResult[0].total;
    const totalPages = Math.ceil(totalUsers / pageSize) || 1;
    page = Math.min(Math.max(1, page), totalPages);
    const offset = (page - 1) * pageSize;
  db.query('SELECT * FROM users WHERE approved=FALSE LIMIT ? OFFSET ?', [pageSize, offset], (err2, users) => {
      if (err2) return res.json({ users: [], totalPages, currentPage: page });
      res.json({ users, totalPages, currentPage: page });
    });
  });
});

// Approve or decline users
app.post('/api/approve', (req, res) => {
  db.query(`UPDATE users SET approved=TRUE WHERE username=?`, [req.body.username], () => res.sendStatus(200));
});

app.post('/api/decline', (req, res) => {
  db.query(`DELETE FROM users WHERE username=?`, [req.body.username], () => res.sendStatus(200));
});

// Approved users
app.get('/api/approved-users', (req, res) => {
  let { page = 1, pageSize = 10 } = req.query;
  page = Number(page);
  pageSize = Number(pageSize);
  db.query('SELECT COUNT(*) AS total FROM users WHERE approved=TRUE', (err, countResult) => {
    if (err) return res.json({ users: [], totalPages: 1, currentPage: page });
    const totalUsers = countResult[0].total;
    const totalPages = Math.ceil(totalUsers / pageSize) || 1;
    page = Math.min(Math.max(1, page), totalPages);
    const offset = (page - 1) * pageSize;
    db.query('SELECT * FROM users WHERE approved=TRUE LIMIT ? OFFSET ?', [pageSize, offset], (err2, users) => {
      if (err2) return res.json({ users: [], totalPages, currentPage: page });
      res.json({ users, totalPages, currentPage: page });
    });
  });
});

// Remove user
app.post('/api/remove-user', (req, res) => {
  db.query(`DELETE FROM users WHERE username=?`, [req.body.username], () => res.sendStatus(200));
});

// Admin info and update
app.get('/api/admin-info', (req, res) => {
  db.query(`SELECT * FROM users WHERE is_admin=TRUE LIMIT 1`, (err, results) => res.json(results[0]));
});

app.post('/api/update-admin', async (req, res) => {
  const { first_name, last_name, email, phone, address, location, afm, new_password, current_password } = req.body;
  db.query(`SELECT * FROM users WHERE is_admin=TRUE LIMIT 1`, async (err, results) => {
    if (err) return res.json({ success: false, message: 'Σφάλμα στην βάση δεδομένων' });
    if (results.length === 0) return res.json({ success: false, message: 'Δεν βρέθηκε διαχειριστής' });
    const admin = results[0];
    const match = await bcrypt.compare(current_password, admin.password);
    if (!match) return res.json({ success: false, message: 'Λάθος τρέχων κωδικός' });
    let updateSql = `
      UPDATE users 
      SET first_name=?, last_name=?, email=?, phone=?, address=?, location=?, afm=?
      WHERE is_admin=TRUE
    `;
    let values = [first_name, last_name, email, phone, address, location, afm];
    if (new_password && new_password.trim() !== '') {
      const hashed = await bcrypt.hash(new_password, 10);
      updateSql = `
        UPDATE users 
        SET first_name=?, last_name=?, email=?, phone=?, address=?, location=?, afm=?, password=?
        WHERE is_admin=TRUE
      `;
      values = [first_name, last_name, email, phone, address, location, afm, hashed];
    }
    db.query(updateSql, values, (err2) => {
      if (err2) return res.json({ success: false, message: 'Αποτυχία ενημέρωσης' });
      res.json({ success: true, message: 'Οι αλλαγές αποθηκεύτηκαν!' });
    });
  });
});


// All auctions with filters and pages
app.get('/api/all-auctions', (req, res) => {
  let { category, description, minPrice, maxPrice, location, status, page = 1, pageSize = 10 } = req.query;
  page = Number(page); pageSize = Number(pageSize);
  const filters = [];
  const params = [];
  if (category) { filters.push('ic.category = ?'); params.push(category); }
  if (description) { filters.push('i.description LIKE ?'); params.push(`%${description}%`); }
  if (minPrice) { filters.push('i.currently >= ?'); params.push(minPrice); }
  if (maxPrice) { filters.push('i.currently <= ?'); params.push(maxPrice); }
  if (location) { filters.push('i.location LIKE ?'); params.push(`%${location}%`); }
  if (status) { filters.push('i.status = ?'); params.push(status); }
  const whereClause = filters.length > 0 ? 'WHERE ' + filters.join(' AND ') : '';
  const countSql = `
    SELECT COUNT(DISTINCT i.id) AS total
    FROM items i
    LEFT JOIN item_categories ic ON i.id = ic.item_id
    ${whereClause}
  `;
  db.query(countSql, params, (err, countResult) => {
    if (err) return res.status(500).json({ error: 'Σφάλμα στην βάση δεδομένων' });
    const totalAuctions = countResult[0].total;
    const totalPages = Math.ceil(totalAuctions / pageSize) || 1;
    page = Math.min(Math.max(1, page), totalPages);
    const offset = (page - 1) * pageSize;
    const sql = `
      SELECT i.id, i.name, i.currently, i.first_bid, i.description, i.location, i.ends, i.status,
             u.username AS seller, IFNULL(u.seller_rating,0) AS seller_rating
      FROM items i
      JOIN users u ON i.seller_username = u.username
      LEFT JOIN item_categories ic ON i.id = ic.item_id
      ${whereClause}
      GROUP BY i.id
      ORDER BY i.ends ASC
      LIMIT ? OFFSET ?
    `;
    db.query(sql, [...params, pageSize, offset], (err2, auctions) => {
      if (err2) return res.status(500).json({ error: 'Σφάλμα στην βάση δεδομένων' });
      const auctionIds = auctions.map(a => a.id);
      if (auctionIds.length === 0) return res.json({ auctions: [], totalPages, currentPage: page });
      // Fetch categories
      db.query('SELECT * FROM item_categories WHERE item_id IN (?)', [auctionIds], (err3, cats) => {
        if (err3) return res.json({ auctions, totalPages, currentPage: page });
        const catMap = {};
        cats.forEach(c => {
          if (!catMap[c.item_id]) catMap[c.item_id] = [];
          catMap[c.item_id].push(c.category);
        });
        // Fetch images
        db.query('SELECT * FROM item_images WHERE item_id IN (?)', [auctionIds], (err4, imgs) => {
          if (err4) return res.json({ auctions, totalPages, currentPage: page });
          const imgMap = {};
          imgs.forEach(img => {
            if (!imgMap[img.item_id]) imgMap[img.item_id] = [];
            imgMap[img.item_id].push(img.image_url);
          });
          auctions.forEach(a => {
            a.categories = catMap[a.id] ? catMap[a.id].join(', ') : '-';
            a.images = imgMap[a.id] ? imgMap[a.id] : [];
          });
          res.json({ auctions, totalPages, currentPage: page, totalAuctions });
        });
      });
    });
  });
});

// Export selected auctions as XML
app.post('/api/auctions/export-xml', async (req, res) => {
  const { auctionIds = [] } = req.body;
  if (!Array.isArray(auctionIds) || auctionIds.length === 0)
    return res.status(400).json({ error: 'Δεν επιλέχθηκαν δημοπρασίες' });
  try {
    // Fetch items
    const [items] = await db.promise().query(`
      SELECT i.id, i.name, i.currently, i.first_bid, i.number_of_bids,
             i.location, i.latitude, i.longitude, i.country,
             i.started, i.ends, i.description,
             u.username AS seller_username, u.seller_rating
      FROM items i
      JOIN users u ON u.username = i.seller_username
      WHERE i.id IN (?)
    `, [auctionIds]);
    // Fetch categories
    const [categories] = await db.promise().query(`
      SELECT item_id, category FROM item_categories WHERE item_id IN (?)
    `, [auctionIds]);
    const categoriesByItem = {};
    categories.forEach(c => {
      if (!categoriesByItem[c.item_id]) categoriesByItem[c.item_id] = [];
      categoriesByItem[c.item_id].push(c.category);
    });
    // Fetch bids
    const [bids] = await db.promise().query(`
      SELECT b.item_id, b.amount, b.time,
             u.username, u.bidder_rating, u.address, u.location
      FROM bids b
      JOIN users u ON u.username = b.bidder_username
      WHERE b.item_id IN (?)
      ORDER BY b.time ASC
    `, [auctionIds]);
    const bidsByItem = {};
    bids.forEach(b => {
      if (!bidsByItem[b.item_id]) bidsByItem[b.item_id] = [];
      bidsByItem[b.item_id].push(b);
    });
    // Helps to format date
    const formatDate = d => {
      const dt = new Date(d);
      if (isNaN(dt.getTime())) return '';
      const months = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];
      return `${months[dt.getMonth()]}-${String(dt.getDate()).padStart(2,'0')}-${String(dt.getFullYear()).slice(-2)} ` +
             `${String(dt.getHours()).padStart(2,'0')}:${String(dt.getMinutes()).padStart(2,'0')}:${String(dt.getSeconds()).padStart(2,'0')}`;
    };
    // Build XML
    let xml = `<Items>\n`;
    items.forEach(item => {
      const currently = parseFloat(item.currently) || 0;
      const firstBid = parseFloat(item.first_bid) || 0;
      const itemCategories = categoriesByItem[item.id] || [];
      const itemBids = bidsByItem[item.id] || [];
      xml += `  <Item ItemID="${item.id}">\n`;
      xml += `    <Name>${item.name || ''}</Name>\n`;
      if (itemCategories.length > 0)
        itemCategories.forEach(cat => xml += `    <Category>${cat}</Category>\n`);
      else
        xml += `    <Category></Category>\n`;
      xml += `    <Currently>$${currently.toFixed(2)}</Currently>\n`;
      xml += `    <First_Bid>$${firstBid.toFixed(2)}</First_Bid>\n`;
      xml += `    <Number_of_Bids>${item.number_of_bids || 0}</Number_of_Bids>\n`;
      // Bids
      if (itemBids.length === 0) {
        xml += `    <Bids />\n`;
      } else {
        xml += `    <Bids>\n`;
        itemBids.forEach(b => {
          const amt = parseFloat(b.amount) || 0;
          xml += `      <Bid>\n`;
          xml += `        <Bidder Rating="${b.bidder_rating || 0}" UserID="${b.username || ''}">\n`;
          xml += `          <Location>${b.address || ''}</Location>\n`;
          xml += `          <Country>${b.location || ''}</Country>\n`;
          xml += `        </Bidder>\n`;
          xml += `        <Time>${formatDate(b.time)}</Time>\n`;
          xml += `        <Amount>$${amt.toFixed(2)}</Amount>\n`;
          xml += `      </Bid>\n`;
        });
        xml += `    </Bids>\n`;
      }
      // Outer Location and if there is Latitude/Longitude
      if (item.latitude != null && item.longitude != null) {
        xml += `    <Location Latitude="${item.latitude}" Longitude="${item.longitude}">${item.location || ''}</Location>\n`;
      } else {
        xml += `    <Location>${item.location || ''}</Location>\n`;
      }
      xml += `    <Country>${item.country || ''}</Country>\n`;
      xml += `    <Started>${formatDate(item.started)}</Started>\n`;
      xml += `    <Ends>${formatDate(item.ends)}</Ends>\n`;
      xml += `    <Seller Rating="${item.seller_rating || 0}" UserID="${item.seller_username || ''}" />\n`;
      xml += `    <Description>${item.description || ''}</Description>\n`;
      xml += `  </Item>\n`;
    });
    xml += `</Items>`;
    res.setHeader('Content-Type', 'application/xml');
    res.setHeader('Content-Disposition', 'attachment; filename="selected_auctions.xml"');
    res.send(xml);
  } catch (err) {
    console.error(err);
    res.status(500).send('Σφάλμα κατά την εξαγωγή XML');
  }
});

// =============== USER PAGES ===============
app.get('/manage', (req, res) => { res.sendFile(path.join(__dirname, 'public', 'manage.html')); });
app.get('/browse', (req, res) => { res.sendFile(path.join(__dirname, 'public', 'browse.html')); });

// Get current logged-in user
app.get('/api/current-user', (req, res) => {
  if (!req.session.username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  res.json({ username: req.session.username });
});


app.get('/api/user-info', (req, res) => {
  if (!req.session.username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  const sql = `SELECT username, first_name, last_name, email, phone, address, location, afm 
               FROM users WHERE username = ?`;
  db.query(sql, [req.session.username], (err, results) => {
    if (err) return res.status(500).json({ error: 'Σφάλμα στην βάση δεδομένων' });
    if (results.length === 0) return res.status(404).json({ error: 'Ο χρήστης δεν βρέθηκε' });
    res.json(results[0]);
  });
});


//Change users info
app.post('/api/user-update', async (req, res) => {
  if (!req.session.username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  const { first_name, last_name, email, phone, address, location, afm, currentPassword, newPassword, confirmPassword } = req.body;
  db.query('SELECT * FROM users WHERE username = ?', [req.session.username], async (err, results) => {
    if (err) return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });
    if (results.length === 0) return res.status(404).json({ error: 'Ο χρήστης δεν βρέθηκε' });
    const user = results[0];
    // If user wants to change password
    let hashedPassword = user.password;
    if (newPassword || confirmPassword || currentPassword) {
      if (!currentPassword || !newPassword || !confirmPassword) {
        return res.status(400).json({ error: 'Συμπληρώστε όλα τα πεδία κωδικού' });
      }
      if (newPassword !== confirmPassword) {
        return res.status(400).json({ error: 'Οι νέοι κωδικοί δεν ταιριάζουν' });
      }
      const match = await bcrypt.compare(currentPassword, user.password);
      if (!match) {
        return res.status(400).json({ error: 'Λάθος τρέχων κωδικός' });
      }
      hashedPassword = await bcrypt.hash(newPassword, 10);
    }
    // Update info
    const updateSql = `
      UPDATE users SET first_name=?, last_name=?, email=?, phone=?, address=?, location=?, afm=?, password=?
      WHERE username=?`;
    db.query(updateSql, [first_name, last_name, email, phone, address, location, afm, hashedPassword, req.session.username], (err2) => {
      if (err2) return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });
      res.json({ success: true });
    });
  });
});



// ================= MANAGE AUCTIONS =================

// Helper to get current MySQL timestamp
function nowMySQL() { return new Date(); }
function toTwoDecimals(n) { return Math.round(Number(n) * 100) / 100; }
function computeStatus(started, ends) {
  const now = nowMySQL();
  if (now < started) return 'scheduled';
  if (now >= started && now <= ends) return 'active';
  if (now > ends) return 'ended';
  return 'scheduled';
}

// Create auctions
app.post('/api/auctions', (req, res) => {
  const seller_username = req.session.username;
  if (!seller_username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  const {
    name,
    categories = [],
    first_bid,
    location,
    latitude = null,
    longitude = null,
    country,
    started,
    ends,
    description,
    images = []
  } = req.body;
  if (!name || !first_bid || !location || !country || !started || !ends) {
    return res.status(400).json({ error: 'Λείπουν απαιτούμενα πεδία' });
  }
  const startedDt = new Date(started);
  const endsDt = new Date(ends);
  if (Number.isNaN(startedDt.getTime()) || Number.isNaN(endsDt.getTime()) || startedDt >= endsDt) {
    return res.status(400).json({ error: 'Μη έγκυρες ημερομηνίες έναρξης/λήξης' });
  }

  const status = computeStatus(startedDt, endsDt);
  const current = toTwoDecimals(first_bid);

  db.query(
    `INSERT INTO items 
     (name, currently, first_bid, number_of_bids, location, latitude, longitude, country, started, ends, seller_username, description, status)
     VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)`,
    [name, current, first_bid, 0, location, latitude, longitude, country, startedDt, endsDt, seller_username, description, status],
    (err, result) => {
      if (err) {
        console.error(err);
        return res.status(500).json({ error: 'Αποτυχία δημιουργίας δημοπρασίας' });
      }
      const itemId = result.insertId;
      // Insert categories
      const cats = Array.isArray(categories) ? categories : String(categories).split(',').map(s => s.trim()).filter(Boolean);
      if (cats.length > 0) {
        const values = cats.map(c => [itemId, c]);
        db.query(`INSERT INTO item_categories (item_id, category) VALUES ?`, [values], () => {});
      }
      // Insert images
      const imgs = Array.isArray(images) ? images : [];
      if (imgs.length > 0) {
        const values = imgs.map(url => [itemId, url]);
        db.query(`INSERT INTO item_images (item_id, image_url) VALUES ?`, [values], () => {});
      }
      res.status(201).json({ id: itemId, status });
    }
  );
});

// Users created auctions
app.get('/api/my-auctions', (req, res) => {
  const seller_username = req.session.username;
  if (!seller_username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  let { filter = 'all', page = 1, pageSize = 5 } = req.query;
  page = Number(page);
  pageSize = Number(pageSize);
  let where = 'seller_username = ?';
  const params = [seller_username];
  if (filter !== 'all') {
    where += ' AND status = ?';
    params.push(filter);
  }

  // Count total auctions for pagination
  db.query(`SELECT COUNT(*) AS total FROM items WHERE ${where}`, params, (err, countResult) => {
    if (err) return res.json({ auctions: [], totalPages: 1, currentPage: page });
    const totalAuctions = countResult[0].total;
    const totalPages = Math.ceil(totalAuctions / pageSize) || 1;
    page = Math.min(Math.max(1, page), totalPages);
    const offset = (page - 1) * pageSize;
    db.query(`SELECT * FROM items WHERE ${where} ORDER BY created_at DESC LIMIT ? OFFSET ?`, [...params, pageSize, offset], (err2, rows) => {
      if (err2) return res.json({ auctions: [], totalPages, currentPage: page });
      // Update status if needed
      const updated = [];
      rows.forEach(r => {
        const s = computeStatus(new Date(r.started), new Date(r.ends));
        if (s !== r.status) updated.push([s, r.id]);
        r.status = s;
      });
      if (updated.length > 0) {
        const queries = updated.map(u => new Promise(resolve => db.query(`UPDATE items SET status=? WHERE id=?`, u, () => resolve())));
        Promise.all(queries).then(() => res.json({ auctions: rows, totalPages, currentPage: page }));
      } else {
        res.json({ auctions: rows, totalPages, currentPage: page });
      }
    });
  });
});

// Get auction details
app.get('/api/auctions/:id', (req, res) => {
  const id = req.params.id;
  db.query(`SELECT * FROM items WHERE id=?`, [id], (err, rows) => {
    if (err || rows.length === 0) return res.status(404).json({ error: 'Δεν βρέθηκε' });
    const item = rows[0];
    const status = computeStatus(new Date(item.started), new Date(item.ends));
    if (status !== item.status) db.query(`UPDATE items SET status=? WHERE id=?`, [status, id], () => {});
    item.status = status;
    db.query(`SELECT category FROM item_categories WHERE item_id=?`, [id], (err2, cats) => {
      db.query(`SELECT image_url FROM item_images WHERE item_id=?`, [id], (err3, imgs) => {
        res.json({ ...item, categories: cats.map(c => c.category), images: imgs.map(i => i.image_url) });
      });
    });
  });
});

// Update auction
app.put('/api/auctions/:id', (req,res)=>{
  const seller_username = req.session.username;
  if(!seller_username) return res.status(401).json({error:'Δεν έχετε συνδεθεί'});
  const id = req.params.id;
  db.query(`SELECT * FROM items WHERE id=?`,[id],(err,rows)=>{
    if(err||rows.length===0) return res.status(404).json({error: 'Δεν βρέθηκε'});
    const item = rows[0];
    if(item.seller_username!==seller_username) return res.status(403).json({error:'Δεν επιτρέπεται'});
    const canEdit = item.number_of_bids===0;
    if(!canEdit) return res.status(400).json({error: 'Δεν μπορείτε να επεξεργάσετε μετά την πρώτη προσφορά'});

    const fields=[], values=[];
    const body=req.body;
    function setField(col,val){fields.push(`${col}=?`); values.push(val);}
    if(body.name!==undefined) setField('name',body.name);
    if(body.first_bid!==undefined){ setField('first_bid',body.first_bid); if(item.number_of_bids===0) setField('currently',body.first_bid); }
    if(body.location!==undefined) setField('location',body.location);
    if(body.latitude!==undefined) setField('latitude',body.latitude);
    if(body.longitude!==undefined) setField('longitude',body.longitude);
    if(body.country!==undefined) setField('country',body.country);
    if(body.description!==undefined) setField('description',body.description);
    if(body.started!==undefined) setField('started',new Date(body.started));
    if(body.ends!==undefined) setField('ends',new Date(body.ends));
    if(fields.length>0){
      db.query(`UPDATE items SET ${fields.join(', ')} WHERE id=?`,[...values,id],(err2)=>{
        if(err2) return res.status(500).json({error:'Update failed'});
        if(body.categories!==undefined){
          const cats = Array.isArray(body.categories)?body.categories: String(body.categories).split(',').map(s=>s.trim()).filter(Boolean);
          db.query(`DELETE FROM item_categories WHERE item_id=?`,[id],()=>{
            if(cats.length>0){
              const vals=cats.map(c=>[id,c]);
              db.query(`INSERT INTO item_categories(item_id,category) VALUES ?`,[vals],()=>{});
            }
          });
        }
        if(body.images!==undefined){
          const imgs = Array.isArray(body.images)?body.images: [];
          db.query(`DELETE FROM item_images WHERE item_id=?`,[id],()=>{
            if(imgs.length>0){
              const vals=imgs.map(url=>[id,url]);
              db.query(`INSERT INTO item_images(item_id,image_url) VALUES ?`,[vals],()=>{});
            }
          });
        }
        res.json({ok:true});
      });
    }else res.json({ok:true});
  });
});

// Delete auction
app.delete('/api/auctions/:id',(req,res)=>{
  const seller_username = req.session.username;
  if(!seller_username) return res.status(401).json({error:'Δεν έχετε συνδεθεί'});
  const id=req.params.id;

  db.query(`SELECT * FROM items WHERE id=?`,[id],(err,rows)=>{
    if(err||rows.length===0) return res.status(404).json({error:'Δεν βρέθηκε'});
    const item = rows[0];
    if(item.seller_username!==seller_username) return res.status(403).json({error:'Δεν επιτρέπεται'});
    const canDelete = item.number_of_bids===0;
    if(!canDelete) return res.status(400).json({error:'Δεν μπορείτε να διαγράψετε μετά την πρώτη προσφορά'});

    db.query(`DELETE FROM items WHERE id=?`,[id],err2=>{
      if(err2) return res.status(500).json({error:'Η διαγραφή απέτυχε'});
      res.json({ok:true});
    });
  });
});


// See all bids from the auction
app.get('/api/auctions/:id/bids', (req, res) => {
  const id = req.params.id;
  db.query(`SELECT * FROM bids WHERE item_id=? ORDER BY time DESC`, [id], (err, rows) => {
    if (err) return res.json([]);
    res.json(rows);
  });
});

// See the bids that the user has done
app.get('/api/my-bids', (req, res) => {
  const username = req.session.username;
  if (!username) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });

  let { page = 1, pageSize = 5 } = req.query;
  page = Number(page);
  pageSize = Number(pageSize);

  db.query('SELECT COUNT(*) AS total FROM bids WHERE bidder_username=?', [username], (err, countResult) => {
    if (err) return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });

    const totalBids = countResult[0].total;
    const totalPages = Math.ceil(totalBids / pageSize) || 1;
    page = Math.min(Math.max(1, page), totalPages);
    const offset = (page - 1) * pageSize;

    const sql = `
      SELECT b.id AS bid_id, b.amount, b.time, i.id AS item_id, i.name AS item_name, i.status
      FROM bids b
      JOIN items i ON b.item_id = i.id
      WHERE b.bidder_username=?
      ORDER BY b.time DESC
      LIMIT ? OFFSET ?
    `;
    db.query(sql, [username, pageSize, offset], (err2, rows) => {
      if (err2) return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });
      res.json({ bids: rows, totalPages, currentPage: page });
    });
  });
});


// ================= BROWSE AUCTIONS =================

// Show all auction and with filters
app.get('/api/auctions', (req, res) => {
  const username = req.session.username ? String(req.session.username) : null;
  const { category, description, minPrice, maxPrice, location, page = 1, pageSize = 10 } = req.query;
  let sql = `
    SELECT i.id, i.name, i.currently, i.description, i.location, i.ends, i.status, u.username AS seller
    FROM items i
    JOIN users u ON i.seller_username = u.username
    LEFT JOIN item_categories ic ON i.id = ic.item_id
    WHERE i.status='active'
  `;
  const params = [];
  if (username) {
    sql += ' AND i.seller_username <> ?';
    params.push(username);
  }
  if (category) { sql += ' AND ic.category = ?'; params.push(category); }
  if (description) { sql += ' AND i.description LIKE ?'; params.push(`%${description}%`); }
  if (minPrice) { sql += ' AND i.currently >= ?'; params.push(minPrice); }
  if (maxPrice) { sql += ' AND i.currently <= ?'; params.push(maxPrice); }
  if (location) { sql += ' AND i.location LIKE ?'; params.push(`%${location}%`); }
  sql += ' GROUP BY i.id';
  const offset = (Number(page) - 1) * Number(pageSize);
  sql += ' ORDER BY i.ends ASC LIMIT ? OFFSET ?';
  params.push(Number(pageSize), offset);
  db.query(sql, params, (err, auctions) => {
    if (err) return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });
    const auctionIds = auctions.map(a => a.id);
    if (auctionIds.length === 0) return res.json({ auctions: [] });
    db.query('SELECT * FROM item_categories WHERE item_id IN (?)', [auctionIds], (err2, cats) => {
      if (err2) return res.json({ auctions });
      const catMap = {};
      cats.forEach(c => {
        if (!catMap[c.item_id]) catMap[c.item_id] = [];
        catMap[c.item_id].push(c.category);
      });
      auctions.forEach(a => {
        a.categories = catMap[a.id] ? catMap[a.id].join(', ') : '-';
      });
      res.json({ auctions });
    });
  });
});

// Add the view of an item to the table visits
app.post('/api/visit', (req, res) => {
  const { username, item_id } = req.body;
  if (!username || !item_id) {
    return res.status(400).json({ error: 'Λείπει όνομα χρήστη ή κωδικός αντικειμένου' });
  }
  db.query(
    'INSERT INTO visits (username, item_id, visit_time) VALUES (?, ?, NOW())',
    [username, item_id],
    (err, result) => {
      if (err) {
        console.error('Σφάλμα κατά την καταχώρηση της επίσκεψης:', err);
        return res.status(500).json({ error: 'Σφάλμα στη βάση δεδομένων' });
      }
      res.json({ success: true });
    }
  );
});



// ================= BIDS =================

// Place a bid on an auction
app.post('/api/auctions/:id/bid', (req, res) => {
  const bidder = req.session.username;
  if (!bidder) return res.status(401).json({ error: 'Δεν έχετε συνδεθεί' });
  const itemId = req.params.id;
  const { amount } = req.body;
  const bidAmount = parseFloat(amount);
  if (isNaN(bidAmount)) return res.status(400).json({ error: 'Μη έγκυρο ποσό προσφοράς' });
  db.query('SELECT * FROM items WHERE id=?', [itemId], (err, rows) => {
    if (err || rows.length === 0) return res.status(404).json({ error: 'Δεν βρέθηκε η δημοπρασία' });
    const auction = rows[0];
    const now = new Date();
    if (now < new Date(auction.started) || now > new Date(auction.ends) || auction.status !== 'active') {
      return res.status(400).json({ error: 'Η δημοπρασία δεν είναι ενεργή' });
    }
    const current = parseFloat(auction.currently) || parseFloat(auction.first_bid) || 0;
    const minBid = Math.max(current, parseFloat(auction.first_bid));
    if (bidAmount < minBid) {
      return res.status(400).json({ error: `Η προσφορά πρέπει να είναι τουλάχιστον ${minBid.toFixed(2)}€` });
    }
    // Insert bid
    db.query(
      'INSERT INTO bids (item_id, bidder_username, amount, time) VALUES (?,?,?,NOW())',
      [itemId, bidder, bidAmount],
      (err2) => {
        if (err2) return res.status(500).json({ error: 'Αποτυχία καταχώρησης προσφοράς' });
        // Update current price & number of bids
        db.query(
          'UPDATE items SET currently=?, number_of_bids=number_of_bids+1 WHERE id=?',
          [bidAmount, itemId],
          (err3) => {
            if (err3) return res.status(500).json({ error: 'Αποτυχία ενημέρωσης δημοπρασίας' });
            return res.json({ ok: true, newCurrent: bidAmount });
          }
        );
      }
    );
  });
});

// Get all bids for an auction
app.get('/api/auctions/:id/bids', (req, res) => {
  const itemId = req.params.id;
  // Here we select bidder_username as is, to match table column
  db.query(
    'SELECT bidder_username, amount, time FROM bids WHERE item_id=? ORDER BY time ASC',
    [itemId],
    (err, rows) => {
      if (err) return res.status(500).json([]);
      res.json(rows); // bidder_username will now correctly appear in frontend
    }
  );
});

// ================= CHAT =================

// Send a message
app.post('/api/messages', (req, res) => {
  const sender = req.session?.username;
  if (!sender) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  const { receiver, subject, body } = req.body;
  if (!receiver || !body) {
    return res.status(400).json({ error: "Απαιτούνται παραλήπτης και περιεχόμενο μηνύματος" });
  }

  db.query(
    'INSERT INTO messages (sender_username, receiver_username, subject, body, showOnRec, showOnSend) VALUES (?,?,?,?,1,1)',
    [sender, receiver, subject, body],
    (err) => {
      if (err) {
        console.error("Error sending message:", err);
        return res.status(500).json({ error: "Αποτυχία αποστολής μηνύματος" });
      }
      res.json({ ok: true, message: "Το μήνυμα στάλθηκε επιτυχώς" });
    }
  );
});

// Get incoming messages (Inbox)
app.get('/api/messages', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  db.query(
    'SELECT * FROM messages WHERE receiver_username=? AND showOnRec=1 ORDER BY sent_at DESC',
    [user],
    (err, rows) => {
      if (err) {
        console.error("Error fetching inbox:", err);
        return res.status(500).json({ error: "Αποτυχία ανάκτησης μηνυμάτων" });
      }
      res.json(rows);
    }
  );
});

// Get sent messages
app.get('/api/messages/sent', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  db.query(
    'SELECT * FROM messages WHERE sender_username=? AND showOnSend=1 ORDER BY sent_at DESC',
    [user],
    (err, rows) => {
      if (err) {
        console.error("Error fetching sent messages:", err);
        return res.status(500).json({ error: "Αποτυχία ανάκτησης εξερχόμενων μηνυμάτων" });
      }
      res.json(rows);
    }
  );
});

// Mark a message as read
app.post('/api/messages/:id/read', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  const msgId = req.params.id;
  db.query(
    'UPDATE messages SET is_read=1 WHERE id=? AND receiver_username=?',
    [msgId, user],
    (err, result) => {
      if (err) {
        console.error("Error updating message:", err);
        return res.status(500).json({ error: "Αποτυχία ενημέρωσης μηνύματος" });
      }
      if (result.affectedRows === 0) {
        return res.status(404).json({ error: "Το μήνυμα δεν βρέθηκε" });
      }
      res.json({ ok: true });
    }
  );
});

// Count unread messages
app.get('/api/messages/unread-count', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  db.query(
    'SELECT COUNT(*) AS cnt FROM messages WHERE receiver_username=? AND is_read=0 AND showOnRec=1',
    [user],
    (err, rows) => {
      if (err) {
        console.error("Error counting unread messages:", err);
        return res.status(500).json({ error: "Αποτυχία καταμέτρησης μηνυμάτων" });
      }
      res.json({ unread: rows[0].cnt });
    }
  );
});

// Recommended users (for sending new messages)
app.get('/api/recommended-users', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  const sql = `
    SELECT DISTINCT u.username, u.seller_rating AS rating, i.id AS item_id, i.name AS item_name
    FROM users u
    JOIN items i ON i.seller_username = u.username
    JOIN bids b ON b.item_id = i.id
    WHERE i.status='ended'
      AND b.bidder_username = ?
      AND b.amount = (SELECT MAX(amount) FROM bids WHERE item_id = i.id)
      AND NOT EXISTS (
        SELECT 1 FROM messages m
        WHERE (m.sender_username=? AND m.receiver_username=u.username)
           OR (m.sender_username=u.username AND m.receiver_username=?)
      )
  `;

  db.query(sql, [user, user, user], (err, rows) => {
    if (err) {
      console.error("Error fetching recommended users:", err);
      return res.status(500).json({ error: "Αποτυχία φόρτωσης προτεινόμενων χρηστών" });
    }
    res.json(rows);
  });
});

// Delete (hide) from inbox
app.post('/api/messages/:id/delete-rec', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });

  const msgId = req.params.id;
  db.query(
    'UPDATE messages SET showOnRec=0 WHERE id=? AND receiver_username=?',
    [msgId, user],
    (err, result) => {
      if (err) {
        console.error("Error hiding message from inbox:", err);
        return res.status(500).json({ error: "Αποτυχία διαγραφής" });
      }
      if (result.affectedRows === 0) {
        return res.status(404).json({ error: "Το μήνυμα δεν βρέθηκε" });
      }
      res.json({ ok: true });
    }
  );
});

// Delete (hide) from sent
app.post('/api/messages/:id/delete-send', (req, res) => {
  const user = req.session?.username;
  if (!user) return res.status(401).json({ error: "Δεν έχετε συνδεθεί" });
  const msgId = req.params.id;
  db.query(
    'UPDATE messages SET showOnSend=0 WHERE id=? AND sender_username=?',
    [msgId, user],
    (err, result) => {
      if (err) {
        console.error("Error hiding message from sent:", err);
        return res.status(500).json({ error: "Αποτυχία διαγραφής" });
      }
      if (result.affectedRows === 0) {
        return res.status(404).json({ error: "Το μήνυμα δεν βρέθηκε" });
      }
      res.json({ ok: true });
    }
  );
});


// ================= RECOMMENDATIONS =================
app.get('/api/recommendations-mf', async (req, res) => {
  const { username, page = 1, pageSize = 10, category, description, minPrice, maxPrice, location } = req.query;
  if (!username) return res.status(400).json({ error: 'Λείπει το όνομα χρήστη' });
  try {
    // Get all the visits which the user has
    const [visits] = await db.promise().query('SELECT username, item_id FROM visits');
    if (!visits.length) return res.json({ recommendations: [], totalPages: 1 });
    // Create the user-item matrix
    const users = [...new Set(visits.map(v => v.username))];
    const items = [...new Set(visits.map(v => v.item_id))];
    const userIndex = Object.fromEntries(users.map((u, i) => [u, i]));
    const itemIndex = Object.fromEntries(items.map((it, i) => [it, i]));
    const R = Array.from({ length: users.length }, () => Array(items.length).fill(0));
    visits.forEach(v => {
      R[userIndex[v.username]][itemIndex[v.item_id]] = 1;
    });
    // Matrix Factorization
    function matrixFactorization(R, K = 3, steps = 200, alpha = 0.01, beta = 0.01) {
      const m = R.length, n = R[0].length;
      let P = Array.from({ length: m }, () => Array(K).fill(0).map(() => Math.random()));
      let Q = Array.from({ length: n }, () => Array(K).fill(0).map(() => Math.random()));
      for (let step = 0; step < steps; step++) {
        for (let i = 0; i < m; i++) {
          for (let j = 0; j < n; j++) {
            if (R[i][j] > 0) {
              let eij = R[i][j] - P[i].reduce((sum, _, k) => sum + P[i][k] * Q[j][k], 0);
              for (let k = 0; k < K; k++) {
                P[i][k] += alpha * (2 * eij * Q[j][k] - beta * P[i][k]);
                Q[j][k] += alpha * (2 * eij * P[i][k] - beta * Q[j][k]);
              }
            }
          }
        }
      }
      return [P, Q];
    }
    const [P, Q] = matrixFactorization(R);
    const currentUserIndex = userIndex[username];
    if (currentUserIndex === undefined) {
      return res.json({ recommendations: [], totalPages: 1 });
    }
    // Calculate the predicted scores
    const scores = items.map((itemId, j) => {
      const score = P[currentUserIndex].reduce((sum, _, k) => sum + P[currentUserIndex][k] * Q[j][k], 0);
      return { itemId, score };
    });
    // Ignore the visit items
    const visitedItems = new Set(visits.filter(v => v.username === username).map(v => v.item_id));
    const filteredScores = scores.filter(s => !visitedItems.has(s.itemId));
    // Grading 
    filteredScores.sort((a, b) => b.score - a.score);
    const totalPages = Math.ceil(filteredScores.length / pageSize);
    const start = (page - 1) * pageSize;
    const pageScores = filteredScores.slice(start, start + parseInt(pageSize));
    if (!pageScores.length) return res.json({ recommendations: [], totalPages });
    // Get items from the database with filters
    const ids = pageScores.map(s => s.itemId);
    let sql = `
      SELECT i.*, u.username AS seller,
             GROUP_CONCAT(ic.category) AS categories
      FROM items i
      JOIN users u ON i.seller_username = u.username
      LEFT JOIN item_categories ic ON i.id = ic.item_id
      WHERE i.id IN (?)
    `;
    const params = [ids];
    if (category) {
      sql += " AND ic.category LIKE ?";
      params.push(`%${category}%`);
    }
    if (description) {
      sql += " AND i.description LIKE ?";
      params.push(`%${description}%`);
    }
    if (minPrice) {
      sql += " AND i.currently >= ?";
      params.push(minPrice);
    }
    if (maxPrice) {
      sql += " AND i.currently <= ?";
      params.push(maxPrice);
    }
    if (location) {
      sql += " AND i.location LIKE ?";
      params.push(`%${location}%`);
    }
    sql += " GROUP BY i.id";
    const [itemsDetails] = await db.promise().query(sql, params);
    res.json({ recommendations: itemsDetails, totalPages });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Σφάλμα διακομιστή' });
  }
});

// Print a log to where the server is running
app.listen(port, () => console.log(`Server running on http://localhost:${port}`));
