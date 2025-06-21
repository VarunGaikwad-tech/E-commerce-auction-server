DROP DATABASE IF EXISTS auction_db;
CREATE DATABASE auction_db;
USE auction_db;

CREATE TABLE users (
  id INT AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(50) UNIQUE NOT NULL,
  password_hash CHAR(64) NOT NULL
);

CREATE TABLE items (
  id INT AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(100) NOT NULL,
  description TEXT, 
  base_price DECIMAL(10,2) NOT NULL
);

-- Add status tracking for items
ALTER TABLE items ADD COLUMN status ENUM('OPEN','CLOSED') DEFAULT 'OPEN';

-- Modified auctions structure
CREATE TABLE auctions (
  id INT AUTO_INCREMENT PRIMARY KEY,
  title VARCHAR(100) NOT NULL,
  start_time DATETIME NOT NULL,
  end_time DATETIME NOT NULL,
  status ENUM('active', 'closed') DEFAULT 'active'
);

-- New mapping table for auction items
CREATE TABLE auction_items (
  auction_id INT NOT NULL,
  item_id INT NOT NULL,
  FOREIGN KEY (auction_id) REFERENCES auctions(id),
  FOREIGN KEY (item_id) REFERENCES items(id),
  PRIMARY KEY (auction_id, item_id)
);

-- Modified bids table
CREATE TABLE bids (
  id INT AUTO_INCREMENT PRIMARY KEY,
  auction_id INT NOT NULL,
  item_id INT NOT NULL,
  user_id INT NOT NULL,
  amount DECIMAL(10,2) NOT NULL,
  bid_time DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (auction_id) REFERENCES auctions(id),
  FOREIGN KEY (item_id) REFERENCES items(id),
  FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Add admin flag to users
ALTER TABLE users ADD COLUMN is_admin BOOLEAN DEFAULT FALSE;

-- Create winners table
CREATE TABLE winners (
  id INT AUTO_INCREMENT PRIMARY KEY,
  auction_id INT NOT NULL,
  item_id INT NOT NULL,
  user_id INT NOT NULL,
  winning_amount DECIMAL(10,2) NOT NULL,
  FOREIGN KEY (auction_id) REFERENCES auctions(id),
  FOREIGN KEY (item_id) REFERENCES items(id),
  FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Add current_item_index to auctions
ALTER TABLE auctions ADD COLUMN current_item_index INT DEFAULT 0;