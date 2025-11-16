/*
 * R307 Fingerprint Voting System with LCD
 * 
 * Wiring:
 * R307:
 *   VCC -> 3.3V, GND -> GND, TX -> Pin 2, RX -> Pin 3
 * 
 * LCD (16x2):
 *   RS -> Pin 13, E -> Pin 12
 *   D4-D7 -> Pins 11,10,9,8
 *   VSS,RW,K -> GND, VDD,A -> 5V, V0 -> GND
 * 
 * Buttons (one side to pin, other to GND):
 *   ENROLL     -> A0
 *   DELETE     -> A1
 *   VOTE       -> A2 (Verify finger then vote)
 *   UP         -> A3 (Navigate ID up)
 *   DOWN       -> A4 (Navigate ID down)
 *   CANDIDATE1 -> Pin 5
 *   CANDIDATE2 -> Pin 6
 *   CANDIDATE3 -> Pin 7
 *   RESULTS    -> A5
 *   RESET_VOTES -> Pin 1 (NEW - Hold to reset vote counts only)
 * 
 * Note: Pin 1 is TX pin. If you need Serial Monitor, use Pin 4 instead.
 * 
 * FUNCTIONALITY:
 * This sketch implements a fingerprint-based voting system using an R307 fingerprint sensor.
 * Users can enroll their fingerprints, vote for one of three candidates, and view voting results.
 * The system uses an LCD display for user interface and EEPROM to store fingerprint data and vote counts.
 * Includes features for managing fingerprints (enrollment/deletion), voting with verification, and resetting vote counts.
 * 
 */

#include <SoftwareSerial.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

LiquidCrystal lcd(13, 12, 11, 10, 9, 8);
SoftwareSerial fingerSerial(2, 3);

// Button pins
#define BTN_ENROLL      A0
#define BTN_DELETE      A1
#define BTN_VOTE        A2
#define BTN_UP          A3
#define BTN_DOWN        A4
#define BTN_RESULTS     A5
#define BTN_RESET_VOTES 1   // NEW BUTTON (TX pin - may conflict with Serial)
#define BTN_CANDIDATE1  5
#define BTN_CANDIDATE2  6
#define BTN_CANDIDATE3  7

// Command codes
#define GET_IMAGE           0x01
#define IMAGE_TO_TZ         0x02
#define SEARCH              0x04
#define CREATE_MODEL        0x05
#define STORE_MODEL         0x06
#define DELETE_MODEL        0x0C
#define VERIFY_PASSWORD     0x13
#define GET_TEMPLATE_COUNT  0x1D

#define HEADER_H            0xEF
#define HEADER_L            0x01
#define PACKAGE_ID          0x01

// EEPROM addresses
#define EEPROM_VOTE1        0
#define EEPROM_VOTE2        1
#define EEPROM_VOTE3        2
#define EEPROM_VOTED_START  10
#define MAX_FINGERPRINTS    25

uint8_t packet[128];
int totalFingerprints = 0;
int vote1 = 0, vote2 = 0, vote3 = 0;

/**
 * setup() - Initialize the Arduino system
 * 
 * Purpose: Initializes all hardware components including the LCD display, fingerprint sensor,
 * button inputs, and EEPROM data. Verifies sensor communication and loads previously stored
 * voting data. Displays startup screen and tests fingerprint sensor connectivity.
 * 
 * Usage: Called once at Arduino startup before entering the main loop.
 */
void setup() {
  Serial.begin(9600);
  
  pinMode(BTN_ENROLL, INPUT_PULLUP);
  pinMode(BTN_DELETE, INPUT_PULLUP);
  pinMode(BTN_VOTE, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_CANDIDATE1, INPUT_PULLUP);
  pinMode(BTN_CANDIDATE2, INPUT_PULLUP);
  pinMode(BTN_CANDIDATE3, INPUT_PULLUP);
  pinMode(BTN_RESULTS, INPUT_PULLUP);
  pinMode(BTN_RESET_VOTES, INPUT_PULLUP);  // NEW
  
  lcd.begin(16, 2);
  delay(100);
  lcd.clear();
  delay(50);
  
  lcd.setCursor(0, 0);
  lcd.print("Fingerprint");
  lcd.setCursor(0, 1);
  lcd.print("Voting System");
  delay(2000);
  
  // Check for full system reset (hold RESULTS on startup)
  bool resetRequested = true;
  for (int i = 0; i < 20; i++) {
    if (digitalRead(BTN_RESULTS) == HIGH) {
      resetRequested = false;
      break;
    }
    delay(100);
  }
  
  if (resetRequested) {
    resetSystem();
  }
  
  // Load votes from EEPROM
  vote1 = EEPROM.read(EEPROM_VOTE1);
  vote2 = EEPROM.read(EEPROM_VOTE2);
  vote3 = EEPROM.read(EEPROM_VOTE3);
  if (vote1 == 0xFF) vote1 = 0;
  if (vote2 == 0xFF) vote2 = 0;
  if (vote3 == 0xFF) vote3 = 0;
  
  lcd.clear();
  lcd.print("Testing sensor");
  
  bool sensorOK = false;
  fingerSerial.begin(57600);
  delay(1000);
  
  for (int i = 0; i < 6; i++) {
    lcd.setCursor(0, 1);
    lcd.print("Attempt ");
    lcd.print(i + 1);
    lcd.print("     ");
    if (verifyPassword()) {
      sensorOK = true;
      lcd.setCursor(0, 1);
      lcd.print("Sensor OK!      ");
      delay(1500);
      break;
    }
    delay(1000);
  }
  
  if (!sensorOK) {
    lcd.clear();
    lcd.print("Sensor Error!");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");
    while(1);
  }
  
  totalFingerprints = getTemplateCount();
  showMainMenu();
}

  }
}

/**
 * loop() - Main event loop handling all user input
 * 
 * Purpose: Continuously monitors all button inputs and dispatches appropriate actions.
 * Handles enrollment, deletion, voting, results display, and vote reset functionality.
 * Implements debouncing to avoid false triggers from button presses.
 * 
 * Usage: Runs repeatedly after setup() completes, forever during Arduino operation.
 */
void loop() {
  if (digitalRead(BTN_ENROLL) == LOW) {
    delay(200);
    enrollFingerprint();
    showMainMenu();
    while(digitalRead(BTN_ENROLL) == LOW) delay(10);
  }
  
  if (digitalRead(BTN_DELETE) == LOW) {
    delay(200);
    deleteByID();
    showMainMenu();
    while(digitalRead(BTN_DELETE) == LOW) delay(10);
  }
  
  if (digitalRead(BTN_VOTE) == LOW) {
    delay(200);
    verifyAndVote();
    showMainMenu();
    while(digitalRead(BTN_VOTE) == LOW) delay(10);
  }
  
  if (digitalRead(BTN_RESULTS) == LOW) {
    delay(200);
    showResults();
    showMainMenu();
    while(digitalRead(BTN_RESULTS) == LOW) delay(10);
  }
  
  // NEW: Check for vote reset button (hold for 3 seconds)
  if (digitalRead(BTN_RESET_VOTES) == LOW) {
    delay(100);  // Debounce
    if (digitalRead(BTN_RESET_VOTES) == LOW) {
      // Wait to see if button is held
      lcd.clear();
      lcd.print("Hold to reset");
      lcd.setCursor(0, 1);
      lcd.print("votes...");
      
      unsigned long holdStart = millis();
      bool stillHeld = true;
      //Timer protected
      while (millis() - holdStart < 3000) {
        if (digitalRead(BTN_RESET_VOTES) == HIGH) {
          stillHeld = false;
          break;
        }
        
        // Show progress
        int dots = (millis() - holdStart) / 750;
        lcd.setCursor(11 + dots, 1);
        lcd.print(".");
        
        delay(50);
      }
      
      if (stillHeld) {
        resetVotesOnly();
      } else {
        lcd.clear();
        lcd.print("Cancelled");
        delay(1000);
      }
      
      showMainMenu();
      while(digitalRead(BTN_RESET_VOTES) == LOW) delay(10);
    }
  }
  
  delay(50);
}

void showMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enrolled: ");
  lcd.print(totalFingerprints);
  lcd.setCursor(0, 1);
  lcd.print("E-D-V-R Ready");
}
// FIXED FUNCTION: Reset only vote counts, keep fingerprints
/**
 * resetVotesOnly() - Reset all vote counts to zero while preserving enrolled fingerprints
 * 
 * Purpose: Clears the voting data (vote counts for all candidates and "has voted" flags)
 * while maintaining the fingerprint database. Allows users to vote again without re-enrollment.
 * Displays progress messages to the user during reset process.
 * 
 * Usage: Called when user holds the RESET_VOTES button for 3 seconds. Used to conduct
 * multiple rounds of voting with the same fingerprint database.
 * Parameters: None
 */
void resetVotesOnly() {
  lcd.clear();
  lcd.print("Resetting Votes");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  Serial.println("\n=== RESETTING VOTE COUNTS ===");
  
  // Clear vote counts
  EEPROM.write(EEPROM_VOTE1, 0);
  EEPROM.write(EEPROM_VOTE2, 0);
  EEPROM.write(EEPROM_VOTE3, 0);
  
  // Reset "has voted" flags for all enrolled users
  for (int i = 0; i < MAX_FINGERPRINTS; i++) {
    byte currentVal = EEPROM.read(EEPROM_VOTED_START + i);
    if (currentVal != 0xFF) {
      // Clear the high bit (0x80) to mark as not voted
      // This removes the "voted" flag while keeping the ID
      byte idOnly = currentVal & 0x7F;  // Clear bit 7, keep bits 0-6
      EEPROM.write(EEPROM_VOTED_START + i, idOnly);
    }
  }
  
  vote1 = 0;
  vote2 = 0;
  vote3 = 0;
  
  Serial.println("Vote counts reset to 0");
  Serial.println("All users can vote again");
  Serial.print("Fingerprints retained: ");
  Serial.println(totalFingerprints);
  
  delay(1000);
  lcd.clear();
  lcd.print("Votes Reset!");
  lcd.setCursor(0, 1);
  lcd.print("Users can revote");
  delay(2500);
}

/**
 * resetSystem() - Perform a complete system reset
 * 
 * Purpose: Clears all data including vote counts, voted flags, and EEPROM storage.
 * Resets the system to factory default state. Used during startup if RESULTS button
 * is held for full reset, or as needed to completely clear all voting records.
 * 
 * Usage: Called during Arduino startup if RESULTS button held, or invoked manually
 * when complete system reset is required.
 * Parameters: None
 */
void resetSystem() {
  lcd.clear();
  lcd.print("System Reset");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  EEPROM.write(EEPROM_VOTE1, 0);
  EEPROM.write(EEPROM_VOTE2, 0);
  EEPROM.write(EEPROM_VOTE3, 0);
  
  for (int i = 0; i < MAX_FINGERPRINTS; i++) {
    EEPROM.write(EEPROM_VOTED_START + i, 0xFF);
  }
  
  vote1 = 0;
  vote2 = 0;
  vote3 = 0;
  
  delay(2000);
  lcd.clear();
  lcd.print("Reset Complete");
  lcd.setCursor(0, 1);
  lcd.print("Restarting...");
  delay(2000);
}

/**
 * verifyAndVote() - Verify fingerprint against database and facilitate voting
 * 
 * Purpose: Prompts user to place finger on sensor, captures fingerprint image,
 * converts to template, searches database for match. If match found and user hasn't
 * voted yet, allows them to select a candidate to vote for. Provides feedback
 * about verification success/failure.
 * 
 * Usage: Called when user presses VOTE button. Handles entire voting workflow.
 * Parameters: None
 */
void verifyAndVote() {
  lcd.clear();
  lcd.print("Place finger");
  lcd.setCursor(0, 1);
  lcd.print("to verify...");
  
  Serial.println("\n=== VERIFY AND VOTE ===");
  
  while (fingerSerial.available()) {
    fingerSerial.read();
  }
  
  if (!waitForFinger(15)) {
    Serial.println("Timeout waiting for finger");
    lcd.clear();
    lcd.print("Timeout!");
    delay(1500);
    return;
  }
  
  Serial.println("Finger detected!");
  
  lcd.clear();
  lcd.print("Verifying...");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  delay(500);
  
  if (!imageToTemplate(1)) {
    Serial.println("Image to template failed!");
    lcd.clear();
    lcd.print("Scan error!");
    lcd.setCursor(0, 1);
    lcd.print("Try again");
    delay(2000);
    return;
  }
  
  Serial.println("Template created!");
  
  lcd.clear();
  lcd.print("Searching...");
  
  while (fingerSerial.available()) {
    fingerSerial.read();
  }
  
  uint8_t searchData[] = {0x01, 0x00, 0x00, 0x00, 0x7F};
  sendCommand(SEARCH, searchData, 5);
  delay(1000);
  
  uint8_t response[32];
  int len = readResponse(response, 3000);
  
  Serial.print("Search response length: ");
  Serial.println(len);
  
  if (len >= 16 && response[9] == 0x00) {
    int matchID = (response[10] << 8) | response[11];
    int confidence = (response[12] << 8) | response[13];
    
    Serial.print("Match found! ID: ");
    Serial.print(matchID);
    Serial.print(", Confidence: ");
    Serial.println(confidence);
    
    // Check if already voted by looking for ID with high bit set
    bool alreadyVoted = false;
    for (int i = 0; i < MAX_FINGERPRINTS; i++) {
      if (EEPROM.read(EEPROM_VOTED_START + i) == (matchID | 0x80)) {
        alreadyVoted = true;
        break;
      }
    }
    
    if (alreadyVoted) {
      Serial.println("Voter has already voted!");
      lcd.clear();
      lcd.print("Already Voted!");
      lcd.setCursor(0, 1);
      lcd.print("ID: ");
      lcd.print(matchID);
      delay(3000);
      return;
    }
    
    lcd.clear();
    lcd.print("Authorized!");
    lcd.setCursor(0, 1);
    lcd.print("ID:");
    lcd.print(matchID);
    lcd.print(" C:");
    lcd.print(confidence);
    delay(2000);
    
    // Show voting options
    castVote(matchID);
    
  } else {
    Serial.print("No match. Response code: ");
    if (len >= 12) {
      Serial.print("0x");
      Serial.println(response[9], HEX);
    } else {
      Serial.println("NO RESPONSE");
    }
    
    lcd.clear();
    lcd.print("Not Authorized");
    lcd.setCursor(0, 1);
    lcd.print("Enroll first!");
    delay(2000);
  }
}

/**
 * castVote() - Display voting options and record vote selection
 * 
 * Purpose: Shows the three candidate buttons and waits for user selection.
 * Records the selected vote to EEPROM, marks the voter as having voted,
 * and displays confirmation. Has 15-second timeout if no candidate selected.
 * 
 * Usage: Called from verifyAndVote() after fingerprint successfully verified.
 * Parameters: voterID (int) - The ID of the verified voter casting the vote
 */
void castVote(int voterID) {
  lcd.clear();
  lcd.print("Vote:");
  lcd.setCursor(0, 1);
  lcd.print("C1   C2   C3");
  
  Serial.println("Waiting for candidate selection...");
  Serial.print("Voter ID ");
  Serial.print(voterID);
  Serial.println(" can now vote");
  
  unsigned long startTime = millis();
  while (millis() - startTime < 15000) {  // 15 second timeout
    
    if (digitalRead(BTN_CANDIDATE1) == LOW) {
      vote1++;
      EEPROM.write(EEPROM_VOTE1, vote1);
      markAsVoted(voterID);
      
      lcd.clear();
      lcd.print("Vote Recorded!");
      lcd.setCursor(0, 1);
      lcd.print("Candidate 1");
      Serial.println("Voted for Candidate 1");
      delay(3000);
      return;
    }
    
    if (digitalRead(BTN_CANDIDATE2) == LOW) {
      vote2++;
      EEPROM.write(EEPROM_VOTE2, vote2);
      markAsVoted(voterID);
      
      lcd.clear();
      lcd.print("Vote Recorded!");
      lcd.setCursor(0, 1);
      lcd.print("Candidate 2");
      Serial.println("Voted for Candidate 2");
      delay(3000);
      return;
    }
    
    if (digitalRead(BTN_CANDIDATE3) == LOW) {
      vote3++;
      EEPROM.write(EEPROM_VOTE3, vote3);
      markAsVoted(voterID);
      
      lcd.clear();
      lcd.print("Vote Recorded!");
      lcd.setCursor(0, 1);
      lcd.print("Candidate 3");
      Serial.println("Voted for Candidate 3");
      delay(3000);
      return;
    }
    
    delay(50);
  }
  
  lcd.clear();
  lcd.print("Vote Timeout");
  lcd.setCursor(0, 1);
  lcd.print("Cancelled");
  Serial.println("Vote timeout - no selection made");
  delay(2000);
}

/**
 * markAsVoted() - Mark a voter ID as having already voted
 * 
 * Purpose: Sets a flag in EEPROM for the given voter ID to prevent duplicate voting.
 * Uses the high bit (0x80) of the ID storage to mark the voted status while keeping
 * the ID intact in the lower 7 bits.
 * 
 * Usage: Called from castVote() after vote is successfully recorded.
 * Parameters: id (int) - The voter ID to mark as voted
 */
void markAsVoted(int id) {
  // Mark this ID as having voted by setting high bit
  for (int i = 0; i < MAX_FINGERPRINTS; i++) {
    byte storedVal = EEPROM.read(EEPROM_VOTED_START + i);
    if (storedVal == id) {
      EEPROM.write(EEPROM_VOTED_START + i, id | 0x80);
      break;
    }
  }
}

/**
 * showResults() - Display voting results and determine winner
 * 
 * Purpose: Shows vote count for each candidate on LCD display, calculates total votes,
 * determines and displays the winner or indicates if there's a tie. Also outputs
 * results to Serial monitor for monitoring. Displays for several seconds before returning.
 * 
 * Usage: Called when user presses RESULTS button from main menu.
 * Parameters: None
 */
void showResults() {
  lcd.clear();
  lcd.print("Results:");
  lcd.setCursor(0, 1);
  lcd.print("C1:");
  lcd.print(vote1);
  lcd.print(" C2:");
  lcd.print(vote2);
  lcd.print(" C3:");
  lcd.print(vote3);
  
  Serial.println("\n=== VOTING RESULTS ===");
  Serial.print("Candidate 1: ");
  Serial.println(vote1);
  Serial.print("Candidate 2: ");
  Serial.println(vote2);
  Serial.print("Candidate 3: ");
  Serial.println(vote3);
  
  delay(3000);
  
  int totalVotes = vote1 + vote2 + vote3;
  
  if (totalVotes == 0) {
    lcd.clear();
    lcd.print("No votes yet!");
    delay(2000);
    return;
  }
  
  lcd.clear();
  lcd.print("Total: ");
  lcd.print(totalVotes);
  lcd.print("/");
  lcd.print(totalFingerprints);
  
  Serial.print("Total votes: ");
  Serial.print(totalVotes);
  Serial.print(" / ");
  Serial.println(totalFingerprints);
  
  delay(3000);
  
  // Determine winner
  int maxVotes = max(vote1, max(vote2, vote3));
  int winnerCount = 0;
  if (vote1 == maxVotes) winnerCount++;
  if (vote2 == maxVotes) winnerCount++;
  if (vote3 == maxVotes) winnerCount++;
  
  lcd.clear();
  if (winnerCount > 1) {
    lcd.print("Tie!");
    lcd.setCursor(0, 1);
    lcd.print("No clear winner");
  } else {
    lcd.print("Winner:");
    lcd.setCursor(0, 1);
    if (vote1 == maxVotes) lcd.print("Candidate 1");
    else if (vote2 == maxVotes) lcd.print("Candidate 2");
    else if (vote3 == maxVotes) lcd.print("Candidate 3");
  }
  delay(4000);
}

/**
 * enrollFingerprint() - Enroll a new fingerprint to the sensor database
 * 
 * Purpose: Allows user to select an ID, captures two fingerprint images (requiring
 * the same finger to be placed twice), creates a template, and stores it in the
 * fingerprint sensor. Includes validation of image quality and template creation.
 * Tracks the enrolled ID in EEPROM as "not voted".
 * 
 * Usage: Called when user presses ENROLL button from main menu. Handles complete
 * enrollment workflow including ID selection, image capture, and storage.
 * Parameters: None
 */
void enrollFingerprint() {
  int id = 1;
  
  lcd.clear();
  lcd.print("Select ID:");
  
  while (true) {
    lcd.setCursor(0, 1);
    lcd.print("ID: ");
    lcd.print(id);
    lcd.print("    ");
    
    if (digitalRead(BTN_UP) == LOW) {
      id++;
      if (id > MAX_FINGERPRINTS) id = 1;
      delay(200);
    }
    
    if (digitalRead(BTN_DOWN) == LOW) {
      id--;
      if (id < 1) id = MAX_FINGERPRINTS;
      delay(200);
    }
    
    if (digitalRead(BTN_ENROLL) == LOW) {
      delay(200);
      break;
    }
    
    if (digitalRead(BTN_DELETE) == LOW) {
      return;
    }
  }
  
  lcd.clear();
  lcd.print("Enrolling ID:");
  lcd.print(id);
  lcd.setCursor(0, 1);
  lcd.print("Place finger");
  delay(1000);
  
  int attempts = 0;
  bool success = false;
  
  while (attempts < 3 && !success) {
    if (waitForFinger(10)) {
      lcd.clear();
      lcd.print("Hold steady...");
      delay(500);
      
      if (imageToTemplate(1)) {
        success = true;
      } else {
        attempts++;
        lcd.clear();
        lcd.print("Try ");
        lcd.print(attempts + 1);
        lcd.print(" of 3");
        delay(1000);
        
        while (checkFinger()) delay(100);
        delay(300);
        
        if (attempts < 3) {
          lcd.clear();
          lcd.print("Place finger");
          delay(500);
        }
      }
    } else {
      lcd.clear();
      lcd.print("Timeout!");
      delay(1500);
      return;
    }
  }
  
  if (!success) {
    lcd.clear();
    lcd.print("Failed!");
    delay(1500);
    return;
  }
  
  lcd.clear();
  lcd.print("Image OK!");
  lcd.setCursor(0, 1);
  lcd.print("Remove finger");
  delay(1000);
  
  int removeAttempts = 0;
  while (checkFinger() && removeAttempts < 50) {
    delay(200);
    removeAttempts++;
  }
  
  if (removeAttempts >= 50) {
    lcd.clear();
    lcd.print("Remove finger!");
    delay(1500);
    return;
  }
  
  delay(1000);
  
  while (fingerSerial.available()) fingerSerial.read();
  
  lcd.clear();
  lcd.print("Place SAME");
  lcd.setCursor(0, 1);
  lcd.print("finger again");
  delay(1000);
  
  attempts = 0;
  success = false;
  
  while (attempts < 3 && !success) {
    if (waitForFinger(15)) {
      lcd.clear();
      lcd.print("Hold steady...");
      delay(1000);
      
      if (imageToTemplate(2)) {
        success = true;
      } else {
        attempts++;
        lcd.clear();
        lcd.print("Try ");
        lcd.print(attempts + 1);
        lcd.print(" of 3");
        delay(1000);
        
        while (checkFinger()) delay(100);
        delay(800);
        
        while (fingerSerial.available()) fingerSerial.read();
        
        if (attempts < 3) {
          lcd.clear();
          lcd.print("Place SAME");
          lcd.setCursor(0, 1);
          lcd.print("finger again");
          delay(1000);
        }
      }
    } else {
      lcd.clear();
      lcd.print("Timeout!");
      delay(1500);
      return;
    }
  }
  
  if (!success) {
    lcd.clear();
    lcd.print("Failed!");
    delay(1000);
    return;
  }
  
  lcd.clear();
  lcd.print("Processing...");
  
  if (!createModel()) {
    lcd.clear();
    lcd.print("Mismatch!");
    delay(1000);
    return;
  }
  
  lcd.clear();
  lcd.print("Storing...");
  
  if (!storeModel(id)) {
    lcd.clear();
    lcd.print("Store failed!");
    delay(1000);
    return;
  }
  
  // Store this ID as "not voted" (without high bit)
  for (int i = 0; i < MAX_FINGERPRINTS; i++) {
    if (EEPROM.read(EEPROM_VOTED_START + i) == 0xFF) {
      EEPROM.write(EEPROM_VOTED_START + i, id);
      break;
    }
  }
  
  lcd.clear();
  lcd.print("SUCCESS!");
  lcd.setCursor(0, 1);
  lcd.print("ID: ");
  lcd.print(id);
  delay(2000);
  
  totalFingerprints = getTemplateCount();
}

/**
 * deleteByID() - Delete an enrolled fingerprint from the sensor database
 * 
 * Purpose: Allows user to select a fingerprint ID using UP/DOWN buttons,
 * then deletes that fingerprint from the sensor memory. Also removes the ID
 * from EEPROM voting tracking. Provides user feedback on deletion success.
 * 
 * Usage: Called when user presses DELETE button from main menu. Handles ID
 * selection and deletion workflow.
 * Parameters: None
 */
void deleteByID() {
  int id = 1;
  
  lcd.clear();
  lcd.print("Delete ID:");
  
  while (true) {
    lcd.setCursor(0, 1);
    lcd.print("ID: ");
    lcd.print(id);
    lcd.print("    ");
    
    if (digitalRead(BTN_UP) == LOW) {
      id++;
      if (id > MAX_FINGERPRINTS) id = 1;
      delay(200);
    }
    
    if (digitalRead(BTN_DOWN) == LOW) {
      id--;
      if (id < 1) id = MAX_FINGERPRINTS;
      delay(200);
    }
    
    if (digitalRead(BTN_DELETE) == LOW) {
      delay(200);
      break;
    }
    
    if (digitalRead(BTN_ENROLL) == LOW) {
      return;
    }
  }
  
  lcd.clear();
  lcd.print("Deleting ID:");
  lcd.print(id);
  delay(1000);
  
  uint8_t deleteData[] = {(uint8_t)(id >> 8), (uint8_t)(id & 0xFF), 0x00, 0x01};
  sendCommand(DELETE_MODEL, deleteData, 4);
  delay(200);
  
  uint8_t response[32];
  int len = readResponse(response, 1000);
  
  if (len >= 12 && response[9] == 0x00) {
    // Remove from voted tracking
    for (int i = 0; i < MAX_FINGERPRINTS; i++) {
      byte storedVal = EEPROM.read(EEPROM_VOTED_START + i);
      if (storedVal == id || storedVal == (id | 0x80)) {
        EEPROM.write(EEPROM_VOTED_START + i, 0xFF);
        break;
      }
    }
    
    lcd.clear();
    lcd.print("Deleted!");
    lcd.setCursor(0, 1);
    lcd.print("ID: ");
    lcd.print(id);
    delay(2000);
  } else {
    lcd.clear();
    lcd.print("Delete failed!");
    delay(2000);
  }
  
  totalFingerprints = getTemplateCount();
}

/**
 * waitForFinger() - Wait for fingerprint sensor to detect a finger
 * 
 * Purpose: Continuously attempts to capture a fingerprint image from the sensor
 * within a specified timeout period. Returns true if finger detected, false if timeout.
 * Essential for all fingerprint capture operations.
 * 
 * Usage: Called before any fingerprint processing (enrollment, verification).
 * Parameters: timeoutSeconds (int) - Maximum seconds to wait for finger presence
 * Returns: bool - True if finger detected and image captured, false if timeout
 */
bool waitForFinger(int timeoutSeconds) {
  int timeout = timeoutSeconds * 10;
  
  while (timeout > 0) {
    while (fingerSerial.available()) fingerSerial.read();
    
    sendCommand(GET_IMAGE, NULL, 0);
    delay(200);
    
    uint8_t response[32];
    int len = readResponse(response, 1000);
    
    if (len >= 12) {
      if (response[9] == 0x00) {
        return true;
      } else if (response[9] == 0x02) {
        timeout--;
        continue;
      }
    }
    timeout--;
  }
  return false;
}

/**
 * checkFinger() - Check if a finger is currently on the sensor
 * 
 * Purpose: Performs a quick check to detect if a finger is present on the sensor.
 * Used to verify when user removes finger during enrollment and to check for
 * ongoing finger contact. Returns immediately with current status.
 * 
 * Usage: Called during enrollment to verify finger removal and during voting.
 * Parameters: None
 * Returns: bool - True if finger detected on sensor, false otherwise
 */
bool checkFinger() {
  while (fingerSerial.available()) fingerSerial.read();
  
  sendCommand(GET_IMAGE, NULL, 0);
  delay(100);
  
  uint8_t response[32];
  int len = readResponse(response, 500);
  
  return (len >= 12 && response[9] == 0x00);
}

/**
 * imageToTemplate() - Convert fingerprint image to template format
 * 
 * Purpose: Processes the previously captured fingerprint image and converts it
 * to a template that can be used for matching or storage. Templates are smaller
 * and more efficient than raw images. Can store in buffer 1 or 2.
 * Displays error messages if image quality is insufficient.
 * 
 * Usage: Called after successful finger detection to process the captured image.
 * Parameters: bufferID (uint8_t) - Template buffer (1 or 2) to store the template
 * Returns: bool - True if template successfully created, false on error
 */
bool imageToTemplate(uint8_t bufferID) {
  uint8_t data[] = {bufferID};
  
  while (fingerSerial.available()) fingerSerial.read();
  
  sendCommand(IMAGE_TO_TZ, data, 1);
  delay(1500);
  
  uint8_t response[64];
  int len = readResponse(response, 5000);
  
  if (len >= 12) {
    uint8_t confirmCode = response[9];
    
    if (confirmCode != 0x00) {
      lcd.clear();
      
      switch(confirmCode) {
        case 0x06:
          lcd.print("Too messy!");
          lcd.setCursor(0, 1);
          lcd.print("Clean sensor");
          break;
        case 0x07:
          lcd.print("Few features");
          lcd.setCursor(0, 1);
          lcd.print("Press harder");
          break;
        default:
          lcd.print("Error: 0x");
          lcd.print(confirmCode, HEX);
          break;
      }
      delay(2500);
    }
    
    return (confirmCode == 0x00);
  }
  
  return false;
}

/**
 * createModel() - Combine two fingerprint templates into a single model
 * 
 * Purpose: Takes the two template buffers (from first and second finger placements)
 * and creates a single fingerprint model by combining them. This matching ensures
 * the same finger was placed twice during enrollment.
 * 
 * Usage: Called during enrollment after both templates are created.
 * Parameters: None
 * Returns: bool - True if model successfully created, false on mismatch or error
 */
bool createModel() {
  sendCommand(CREATE_MODEL, NULL, 0);
  delay(200);
  
  uint8_t response[32];
  int len = readResponse(response, 1000);
  
  return (len >= 12 && response[9] == 0x00);
}

/**
 * storeModel() - Store the fingerprint model in sensor memory
 * 
 * Purpose: Writes the created fingerprint model to the sensor's internal storage
 * at the specified ID location. Once stored, the fingerprint can be used for
 * matching and searching. This is the final step of enrollment.
 * 
 * Usage: Called during enrollment after model is successfully created.
 * Parameters: id (int) - The ID to store the fingerprint model at (1-25)
 * Returns: bool - True if model stored successfully, false on storage error
 */
bool storeModel(int id) {
  uint8_t data[] = {0x01, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)};
  sendCommand(STORE_MODEL, data, 3);
  delay(200);
  
  uint8_t response[32];
  int len = readResponse(response, 1000);
  
  return (len >= 12 && response[9] == 0x00);
}

/**
 * verifyPassword() - Verify sensor communication with default password
 * 
 * Purpose: Sends default password (0x00000000) to fingerprint sensor to verify
 * that communication is properly established and the sensor is responsive.
 * Used during startup to verify sensor is functional before proceeding.
 * 
 * Usage: Called during setup() to test sensor connectivity.
 * Parameters: None
 * Returns: bool - True if sensor responds correctly, false if communication fails
 */
bool verifyPassword() {
  uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
  sendCommand(VERIFY_PASSWORD, data, 4);
  delay(500);
  
  uint8_t response[32];
  int len = readResponse(response, 2000);
  
  return (len >= 12 && response[9] == 0x00);
}

/**
 * getTemplateCount() - Query sensor for number of enrolled fingerprints
 * 
 * Purpose: Requests the fingerprint sensor to return the count of currently stored
 * fingerprint templates. Used to update UI and track enrollment status.
 * 
 * Usage: Called during setup and after enrollment/deletion operations.
 * Parameters: None
 * Returns: int - Number of fingerprints currently stored in sensor (0-25)
 */
int getTemplateCount() {
  sendCommand(GET_TEMPLATE_COUNT, NULL, 0);
  delay(100);
  
  uint8_t response[32];
  int len = readResponse(response, 1000);
  
  if (len >= 14 && response[9] == 0x00) {
    return (response[10] << 8) | response[11];
  }
  return 0;
}

/**
 * sendCommand() - Send a formatted command packet to the fingerprint sensor
 * 
 * Purpose: Constructs a properly formatted command packet with header, packet ID,
 * command code, data, and checksum according to R307 sensor protocol. Sends the
 * complete packet via SoftwareSerial to the fingerprint sensor.
 * 
 * Usage: Called before every sensor operation to transmit commands.
 * Parameters: 
 *   - cmd (uint8_t): Command code (GET_IMAGE, IMAGE_TO_TZ, SEARCH, etc.)
 *   - data (uint8_t*): Pointer to command data array (NULL if no data)
 *   - dataLen (int): Length of data array
 */
void sendCommand(uint8_t cmd, uint8_t *data, int dataLen) {
  while (fingerSerial.available()) fingerSerial.read();
  
  int idx = 0;
  packet[idx++] = HEADER_H;
  packet[idx++] = HEADER_L;
  packet[idx++] = 0xFF;
  packet[idx++] = 0xFF;
  packet[idx++] = 0xFF;
  packet[idx++] = 0xFF;
  packet[idx++] = PACKAGE_ID;
  
  int length = dataLen + 3;
  packet[idx++] = (length >> 8) & 0xFF;
  packet[idx++] = length & 0xFF;
  packet[idx++] = cmd;
  
  for (int i = 0; i < dataLen; i++) {
    packet[idx++] = data[i];
  }
  
  uint16_t checksum = PACKAGE_ID + ((length >> 8) & 0xFF) + (length & 0xFF) + cmd;
  for (int i = 0; i < dataLen; i++) {
    checksum += data[i];
  }
  
  packet[idx++] = (checksum >> 8) & 0xFF;
  packet[idx++] = checksum & 0xFF;
  
  fingerSerial.write(packet, idx);
}

/**
 * readResponse() - Read response packet from fingerprint sensor
 * 
 * Purpose: Reads incoming data from the sensor via SoftwareSerial into a buffer.
 * Waits up to the specified timeout period for response data. Returns the number
 * of bytes read. Used after every command to retrieve sensor responses.
 * 
 * Usage: Called after each sendCommand() to receive the sensor's response.
 * Parameters:
 *   - buffer (uint8_t*): Array to store received response data
 *   - timeout (int): Maximum milliseconds to wait for response data
 * Returns: int - Number of bytes received in buffer
 */
int readResponse(uint8_t *buffer, int timeout) {
  unsigned long startTime = millis();
  int idx = 0;
  
  while (millis() - startTime < timeout) {
    if (fingerSerial.available()) {
      buffer[idx++] = fingerSerial.read();
      if (idx >= 32) break;
    }
  }
  return idx;
}