import sqlite3
import requests
import os

# Configuration
DB_FILE = 'solar_details.db'
THINGSPEAK_CHANNEL_ID = '2841130'
# Fetching last 100 results for initial population
URL = f"https://api.thingspeak.com/channels/{THINGSPEAK_CHANNEL_ID}/feeds.json?results=100"

def init_db():
    """Initialize the database and create the table if it doesn't exist."""
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        
        # Create table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS solar_readings (
                id INTEGER PRIMARY KEY,
                timestamp TEXT,
                temperature REAL,
                humidity REAL,
                voltage REAL
            )
        ''')
        
        conn.commit()
        print("Database initialized successfully.")
        return conn
    except sqlite3.Error as e:
        print(f"Database error: {e}")
        return None

def sync_data(conn):
    """Fetch data from ThingSpeak and insert into the database."""
    try:
        print(f"Fetching data from ThingSpeak...")
        response = requests.get(URL)
        if response.status_code != 200:
            print(f"Failed to fetch data: {response.status_code}")
            return

        data = response.json()
        feeds = data.get('feeds', [])
        
        print(f"Found {len(feeds)} records. Syncing...")
        
        cursor = conn.cursor()
        new_records = 0
        
        for feed in feeds:
            entry_id = feed.get('entry_id')
            created_at = feed.get('created_at')
            temp = feed.get('field1')
            hum = feed.get('field2')
            volt = feed.get('field3')
            
            # Ensure values are not None before conversion
            temp = float(temp) if temp and temp.strip() else 0.0
            hum = float(hum) if hum and hum.strip() else 0.0
            volt = float(volt) if volt and volt.strip() else 0.0

            # Insert or Ignore to avoid duplicates (assuming entry_id is unique)
            try:
                cursor.execute('''
                    INSERT OR IGNORE INTO solar_readings (id, timestamp, temperature, humidity, voltage)
                    VALUES (?, ?, ?, ?, ?)
                ''', (entry_id, created_at, temp, hum, volt))
                
                if cursor.rowcount > 0:
                    new_records += 1
            except sqlite3.Error as e:
                print(f"Error inserting row {entry_id}: {e}")

        conn.commit()
        print(f"Sync complete. {new_records} new records added.")
        return new_records

    except Exception as e:
        print(f"Sync error: {e}")

import csv

def export_to_csv(conn):
    """Export the database content to a CSV file."""
    try:
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM solar_readings")
        rows = cursor.fetchall()

        if not rows:
            print("No data to export.")
            return

        csv_file = 'solar_data.csv'
        with open(csv_file, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            # Write header
            writer.writerow(['ID', 'Timestamp', 'Temperature', 'Humidity', 'Voltage'])
            # Write rows
            writer.writerows(rows)
        
        print(f"Data successfully exported to {csv_file} ({len(rows)} records).")
    except Exception as e:
        print(f"Export error: {e}")

if __name__ == "__main__":
    connection = init_db()
    if connection:
        new_count = sync_data(connection)
        
        # Update CSV if we have new data OR if the file doesn't exist yet
        if new_count is not None and (new_count > 0 or not os.path.exists('solar_data.csv')):
            export_to_csv(connection)
        else:
            print("No new data found. CSV export skipped.")
            
        connection.close()
