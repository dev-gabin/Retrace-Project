-- MariaDB 10.3+, run as a database administrator.
-- Safe to re-run: existing observations are never reset.
CREATE DATABASE IF NOT EXISTS retrace
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE retrace;

CREATE TABLE IF NOT EXISTS items (
  item VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  display_name VARCHAR(64) NOT NULL,
  PRIMARY KEY (item),
  CONSTRAINT ck_item_id CHECK (item REGEXP '^[a-z][a-z0-9_]*$')
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS last_seen (
  item VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  pos_x INT NULL,
  pos_y INT NULL,
  seen_at DATETIME(6) NOT NULL COMMENT 'UTC observation time, not insertion time',
  snapshot VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NULL,
  drawer_id TINYINT NULL,
  state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (item),
  CONSTRAINT fk_last_seen_item FOREIGN KEY (item) REFERENCES items(item)
    ON UPDATE RESTRICT ON DELETE RESTRICT,
  CONSTRAINT ck_coordinates CHECK (
    (pos_x IS NULL AND pos_y IS NULL) OR
    (pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_x >= 0 AND pos_y >= 0)
  ),
  CONSTRAINT ck_drawer CHECK (drawer_id IS NULL OR drawer_id BETWEEN 1 AND 6),
  CONSTRAINT ck_state CHECK (state IN ('visible', 'occluded', 'uncertain')),
  CONSTRAINT ck_snapshot CHECK (
    snapshot IS NULL OR snapshot REGEXP '^snapshots/[A-Za-z0-9_-]+[.]jpg$'
  )
) ENGINE=InnoDB;

INSERT INTO items (item, display_name) VALUES
 ('carkey', '차키'), ('airpods', '에어팟'), ('wallet', '지갑')
ON DUPLICATE KEY UPDATE item=VALUES(item);
