#include <winsock2.h> 
#include <ws2tcpip.h> 
#include <mysql.h>    
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>
#include <string>
#include <algorithm>
#include <iomanip>

// Color definitions [ ANSI escape codes]
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define BOLD          "\033[1m"

std::string get_current_auction_details(MYSQL* conn);

struct ClientInfo {
    SOCKET socket;                      //used to send/receive messages
    sockaddr_in address;                //Lets the server know where the client is
    std::string username;               // to get username which is displayed on the server
};

#pragma comment(lib, "ws2_32.lib")         //tell compiler to link libraries automatically[fn: SOCKET, connect(), send(), etc]
#pragma comment(lib, "libmysql.lib")        //MySQL client library required to use mysql.h functions

std::vector<SOCKET> clients;                //A dynamic list of all connected clients' sockets.
std::mutex clients_mutex;                   //ensures only 1 thread modifes client vector at a time
std::mutex db_mutex;                        //Prevents multiple threads for using mySQL connection at same time
std::mutex log_mutex;                       //ensures clean logging output 

//details of the db to connect to mySQL db
const char* DB_HOST = "localhost";          
const char* DB_USER = "root";
const char* DB_PASS = "varun@18";
const char* DB_NAME = "auction_db";
const int PORT = 8080;              //port no where auction server listens on ,Clients must connect using this port.

MYSQL* connect_to_db() {
    MYSQL* conn = mysql_init(nullptr);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, nullptr, 0)) {   //Attempts to connect to mySQL db
        std::cerr << "MySQL Error: " << mysql_error(conn) << std::endl;
        exit(1);
    }
    return conn;
}

void execute_query(MYSQL* conn, const std::string& query) {
    std::lock_guard<std::mutex> guard(db_mutex);                          //locks the database so that only 1 thread can read/write at a time
    if (mysql_query(conn, query.c_str())) {
        std::cerr << "Query Error: " << mysql_error(conn) << std::endl;
    }
}

std::string get_current_auction_details(MYSQL* conn) {
    std::lock_guard<std::mutex> db_guard(db_mutex);
    
    // Fetch current auction item index
    std::string query = "SELECT current_item_index FROM auctions WHERE id=1";
    if (mysql_query(conn, query.c_str())) {
        return "ERROR: Failed to fetch auction status";
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0) {
        mysql_free_result(result);
        return "ERROR: No active auction";
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int current_index = std::stoi(row[0]);
    mysql_free_result(result);

    // Fetch item details
    query = 
        "SELECT i.id, i.name, i.description, i.base_price "
        "FROM items i "
        "INNER JOIN auction_items ai ON i.id = ai.item_id "
        "WHERE ai.auction_id = 1 AND ai.item_order = " + std::to_string(current_index + 1);

    if (mysql_query(conn, query.c_str())) {
        return "ERROR: Failed to fetch item details";
    }

    result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0) {
        mysql_free_result(result);
        return "ERROR: No active auction item";
    }

    row = mysql_fetch_row(result);
    std::string item_id = row[0];
    std::string item_name = row[1];
    std::string description = row[2];
    std::string base_price = row[3];
    mysql_free_result(result);

    // Format message with colors
    std::stringstream ss;
    ss << COLOR_CYAN BOLD "\n[ACTIVE AUCTION ITEM]" COLOR_RESET "\n"
       << "Auction ID: " COLOR_GREEN "1" COLOR_RESET "\n"
       << "Item ID: " COLOR_GREEN << item_id << COLOR_RESET "\n"
       << "Item: " COLOR_YELLOW << item_name << COLOR_RESET "\n"
       << "Description: " << description << "\n"
       << "Base Price: " COLOR_GREEN "$" << base_price << COLOR_RESET "\n"
       << std::string(60, '-') << "\n";

    return ss.str();
}

void handle_client(ClientInfo client_info){
    char buffer[1024];
    MYSQL* conn = connect_to_db();
    SOCKET client_socket = client_info.socket;
    sockaddr_in client_addr = client_info.address;

    //Send welcome message
    std::string welcome_msg = std::string(60, '=') + "\n" +
                            COLOR_CYAN BOLD "  WELCOME TO AUCTION SERVER  \n" COLOR_RESET +
                            COLOR_GREEN "Available commands:\n" 
                            "1. LOGIN <username> <password>\n"
                            "2. BID <auction_id> <item_id> <amount>\n" 
                            COLOR_MAGENTA "3. ADMIN_CLOSE_ITEM (admin only)\n" COLOR_RESET +
                            std::string(60, '=') + "\n";
    send(client_socket, welcome_msg.c_str(), welcome_msg.size(), 0);
        
    // Send current auction details
    std::string auction_details = get_current_auction_details(conn);
    send(client_socket, auction_details.c_str(), auction_details.size(), 0);

    bool is_authenticated = false;
    int current_user_id = -1;  // Track logged-in user

    while (true) {
        int bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) break;

        std::string request(buffer, bytes_read);
        std::istringstream iss(request);
        std::string command;
        iss >> command;

        if (command == "LOGIN") {
            std::string username, password;
            if (!(iss >> username >> password)) {
                send(client_socket, "ERROR: Invalid LOGIN format. Use: LOGIN <username> <password>", 61, 0);
                continue;
            }
        
            std::string hashed_password = "hash_" + password;
            std::string query = "SELECT id, username FROM users WHERE username='" + username + 
                              "' AND password_hash='" + hashed_password + "'";
            
            // Get client IP for logging
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            {
                std::lock_guard<std::mutex> log_guard(log_mutex); // ADD THIS
                std::cerr << COLOR_YELLOW << "\n[LOGIN ATTEMPT] From: " << client_ip
                        << COLOR_RESET << "\nUsername: " << username 
                        << "\n" << std::string(40, '~') << std::endl;
            }

            if (mysql_query(conn, query.c_str())) {
                std::cerr << COLOR_RED << "[LOGIN FAILED] Database error: " 
                         << mysql_error(conn) << COLOR_RESET << std::endl;
                send(client_socket, "LOGIN_FAILED (Server Error)", 27, 0);
                continue;
            }
        
            MYSQL_RES* result = mysql_store_result(conn);
            if (mysql_num_rows(result) > 0) {
                MYSQL_ROW row = mysql_fetch_row(result);
                current_user_id = std::stoi(row[0]);  // Store user ID
                is_authenticated = true;
                client_info.username = row[1] ? row[1] : "Unknown";  
                
                {
                    std::lock_guard<std::mutex> log_guard(log_mutex); 
                    std::cerr << COLOR_GREEN << "[LOGIN SUCCESS] User ID: " << current_user_id 
                            << " | Username: " << client_info.username 
                            << COLOR_RESET << std::endl;
                }
                std::string success_msg = "LOGIN_SUCCESS (User ID: " + 
                                         std::to_string(current_user_id) + ")";
                send(client_socket, success_msg.c_str(), success_msg.size(), 0);
            } else {
                {
                    std::lock_guard<std::mutex> log_guard(log_mutex); // ADD THIS
                    std::cerr << COLOR_RED << "[LOGIN FAILED] Invalid credentials for: " 
                            << username << COLOR_RESET << std::endl;
                }
                std::string login_fail_msg = "LOGIN_FAILED (Invalid Credentials)";
                send(client_socket, login_fail_msg.c_str(), login_fail_msg.size(), 0);
            }
            mysql_free_result(result);
        }
        else if (command == "BID") {
            int auction_id, item_id;
            float amount;

            if (!is_authenticated) {
                send(client_socket, "ERROR: You must login first!", 27, 0);
                continue;
            }

            // Admin check start
            std::string admin_check = "SELECT is_admin FROM users WHERE id = " + 
                            std::to_string(current_user_id);
            execute_query(conn, admin_check);
            MYSQL_RES* admin_res = mysql_store_result(conn);
            bool is_admin = false;
            
            if (admin_res && mysql_num_rows(admin_res) > 0) {
                MYSQL_ROW admin_row = mysql_fetch_row(admin_res);
                is_admin = (admin_row && admin_row[0] && std::stoi(admin_row[0]) == 1);
            }
            mysql_free_result(admin_res);
            
            if (is_admin) {
                send(client_socket,"ERROR: Admins cannot place bids",strlen("ERROR: Admins cannot place bids"), 0);
                continue;
            }
            // Admin check end
        
            if (!(iss >> auction_id >> item_id >> amount)) {
                send(client_socket, "ERROR: Usage - BID <auction_id> <item_id> <amount>", 51, 0);
                continue;
            }

            // Get item details for logging
            std::string item_query = "SELECT i.name FROM items i "
                                    "INNER JOIN auction_items ai ON i.id = ai.item_id "
                                    "WHERE ai.auction_id = " + std::to_string(auction_id) + 
                                    " AND ai.item_id = " + std::to_string(item_id);
          
            execute_query(conn, item_query);
            MYSQL_RES* item_res = mysql_store_result(conn);
            if (!item_res || mysql_num_rows(item_res) == 0) {
                send(client_socket, "BID_REJECTED (Invalid Item-Auction Combination)", 45, 0);
                mysql_free_result(item_res);
                continue;
            }
            
            MYSQL_ROW item_row = mysql_fetch_row(item_res);
            std::string item_name(item_row[0]);
            mysql_free_result(item_res);  

            std::string curr_item_query = "SELECT current_item_index FROM auctions WHERE id=1";
            execute_query(conn, curr_item_query);
            MYSQL_RES* curr_item_res = mysql_store_result(conn);
            if (!curr_item_res || mysql_num_rows(curr_item_res) == 0) {
                send(client_socket, "BID_REJECTED (Auction Not Found)", 25, 0);
                mysql_free_result(curr_item_res);
                continue;
            }
            MYSQL_ROW curr_item_row = mysql_fetch_row(curr_item_res);
            int current_index = std::stoi(curr_item_row[0]);
            mysql_free_result(curr_item_res);
        
            // Validate current_index
            if (current_index < 0) {
                send(client_socket, "BID_REJECTED (Auction Not Started)", 29, 0);
                continue;
            }
        
            // Calculate 1-based item_order
            int current_item_order = current_index + 1;
        
            // Fetch actual item_id of the current active item
            std::string current_item_id_query = 
                "SELECT item_id FROM auction_items "
                "WHERE auction_id = " + std::to_string(auction_id) + 
                " AND item_order = " + std::to_string(current_item_order);
            execute_query(conn, current_item_id_query);
            MYSQL_RES* current_item_id_res = mysql_store_result(conn);
            if (!current_item_id_res || mysql_num_rows(current_item_id_res) == 0) {
                send(client_socket, "BID_REJECTED (No Active Item)", 25, 0);
                mysql_free_result(current_item_id_res);
                continue;
            }
            MYSQL_ROW current_item_id_row = mysql_fetch_row(current_item_id_res);
            int current_active_item_id = std::stoi(current_item_id_row[0]);
            mysql_free_result(current_item_id_res);
        
            // Check if bid is for the ACTIVE item
            if (item_id != current_active_item_id) {
                std::string error_msg = "BID_REJECTED (Current item is " + 
                                      std::to_string(current_active_item_id) + ")";
                send(client_socket, error_msg.c_str(), error_msg.size(), 0);
                continue;
            }

            std::string base_price_query = 
            "SELECT i.base_price FROM items i "
            "WHERE i.id = " + std::to_string(item_id);
        
            execute_query(conn, base_price_query);
            MYSQL_RES* base_price_res = mysql_store_result(conn);
            float base_price = 0.0f;
            if (base_price_res && mysql_num_rows(base_price_res) > 0) {
                MYSQL_ROW row = mysql_fetch_row(base_price_res);
                base_price = row[0] ? std::stof(row[0]) : 0.0f;
            }
            mysql_free_result(base_price_res);
            
            if (amount < base_price) {
                std::string error_msg = COLOR_RED "BID_REJECTED (Bid below base price)" COLOR_RESET;
                send(client_socket, error_msg.c_str(), error_msg.size(), 0);
                continue;
            } 
            
            // Format bid information
            {
                std::lock_guard<std::mutex> log_guard(log_mutex); 
                std::cerr << COLOR_CYAN << "\n[NEW BID] " << COLOR_RESET
                        << "User: " << BOLD << client_info.username << COLOR_RESET
                        << " (ID: " << current_user_id << ")\n"
                        << "Auction: " << auction_id << " | Item: " << BOLD << item_name 
                        << COLOR_RESET << " (ID: " << item_id << ")\n"
                        << "Amount: " << COLOR_GREEN << "$" << std::fixed << std::setprecision(2) << amount 
                        << COLOR_RESET << "\n" << std::string(40, '-') << std::endl;
            }

            // Bid validation logic
            std::string max_bid_query = "SELECT MAX(amount) FROM bids WHERE auction_id = " + 
                                      std::to_string(auction_id) + " AND item_id = " + 
                                      std::to_string(item_id);
            
            execute_query(conn, max_bid_query);
            MYSQL_RES* max_bid_result = mysql_store_result(conn);
            float current_max = 0.0f;
            if (max_bid_result) {
                MYSQL_ROW row = mysql_fetch_row(max_bid_result);
                current_max = (row && row[0]) ? std::stof(row[0]) : 0.0f;
                mysql_free_result(max_bid_result);
            }
            if (amount > current_max) {
                // Database transaction
                execute_query(conn, "START TRANSACTION");
                std::string bid_query = "INSERT INTO bids (auction_id, item_id, user_id, amount) VALUES (" +
                                      std::to_string(auction_id) + ", " +
                                      std::to_string(item_id) + ", " +
                                      std::to_string(current_user_id) + ", " +  
                                      std::to_string(amount) + ")";
                
                if (mysql_query(conn, bid_query.c_str())) {
                    mysql_query(conn, "ROLLBACK");
                    send(client_socket, "BID_REJECTED (Server Error)", 27, 0);
                    continue;
                }

                execute_query(conn, "COMMIT");

                // Format broadcast message
                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << amount;
                std::string msg = COLOR_GREEN "BID_UPDATE " COLOR_RESET "Auction " + 
                    std::to_string(auction_id) +
                    " | Item: " + item_name + 
                    " | New High Bid: " COLOR_GREEN "$" + ss.str() + COLOR_RESET;
                
                {
                    std::lock_guard<std::mutex> guard(clients_mutex);
                    for (SOCKET client : clients) {
                        if (client != client_socket) {
                            send(client, msg.c_str(), msg.size(), 0);
                        }
                    }
                }
                send(client_socket, "BID_ACCEPTED", 12, 0);
            } else {
                std::string bid_reject_msg = COLOR_RED "BID_REJECTED (Bid too low)" COLOR_RESET;
                send(client_socket, bid_reject_msg.c_str(), bid_reject_msg.size(), 0);
            }
        }
        else if (command == "ADMIN_CLOSE_ITEM") {
            // Verify admin privileges
            std::string admin_check = "SELECT is_admin FROM users WHERE id = " + 
                                    std::to_string(current_user_id);
            if (mysql_query(conn, admin_check.c_str())) {
                std::cerr << "Admin check failed: " << mysql_error(conn) << "\n";
                send(client_socket, "ERROR: Server error", 18, 0);
                continue;
            }
        
            MYSQL_RES* admin_res = mysql_store_result(conn);
            bool is_admin = false;
            if (admin_res && mysql_num_rows(admin_res) > 0) {
                MYSQL_ROW admin_row = mysql_fetch_row(admin_res);
                is_admin = admin_row && admin_row[0] && std::stoi(admin_row[0]) == 1;
            }
            mysql_free_result(admin_res);
        
            if (!is_authenticated || !is_admin) {
                send(client_socket, "ERROR: Admin privileges required!", 31, 0);
                continue;
            }
        
            // Start transaction
            execute_query(conn, "START TRANSACTION");
        
            try {
                // Get auction state
                std::string auction_query = "SELECT current_item_index, item_count FROM auctions WHERE id = 1 FOR UPDATE"; 
                if (mysql_query(conn, auction_query.c_str())) {
                    throw std::runtime_error("Auction query failed: " + std::string(mysql_error(conn)));
                }
        
                MYSQL_RES* auction_res = mysql_store_result(conn);
                if (!auction_res || mysql_num_rows(auction_res) == 0) {
                    mysql_free_result(auction_res);
                    throw std::runtime_error("No auction found");
                }
        
                MYSQL_ROW auction_row = mysql_fetch_row(auction_res);
                if (!auction_row || !auction_row[0] || !auction_row[1]) {
                    mysql_free_result(auction_res);
                    throw std::runtime_error("Invalid auction data");
                }
        
                int current_index = std::stoi(auction_row[0]);
                int total_items = std::stoi(auction_row[1]);
                mysql_free_result(auction_res);
        
                // Validate item count
                if (total_items <= 0) {
                    throw std::runtime_error("Auction has no items");
                }
        
                // Determine winner
                std::string winner_query = 
                    "SELECT b.user_id, u.username, b.amount, b.bid_time "
                    "FROM bids b "
                    "JOIN users u ON b.user_id = u.id "
                    "WHERE b.auction_id = 1 AND b.item_id = " + std::to_string(current_index + 1) + " "
                    "ORDER BY b.amount DESC, b.bid_time ASC "  
                    "LIMIT 1";
        
                if (mysql_query(conn, winner_query.c_str())) {
                    throw std::runtime_error("Winner query failed: " + std::string(mysql_error(conn)));
                }
        
                MYSQL_RES* winner_res = mysql_store_result(conn);
                std::string winner_id = "None";
                std::string winner_name = "None";
                std::string winner_amount = "0";
        
                if (winner_res && mysql_num_rows(winner_res) > 0) {
                    MYSQL_ROW winner_row = mysql_fetch_row(winner_res);
                    if (winner_row) {
                        winner_id = winner_row[0] ? winner_row[0] : "None";
                        winner_name = winner_row[1] ? winner_row[1] : "Unknown";
                        winner_amount = winner_row[2] ? winner_row[2] : "0";
        
                        // Insert winner
                        std::string insert_winner = 
                            "INSERT INTO winners (auction_id, item_id, user_id, winning_amount) "
                            "VALUES (1, " + std::to_string(current_index + 1) + ", " +
                            winner_id + ", " + winner_amount + ")";
                        
                        if (mysql_query(conn, insert_winner.c_str())) {
                            throw std::runtime_error("Winner insertion failed: " + std::string(mysql_error(conn)));
                        }
        
                        {
                            std::lock_guard<std::mutex> log_guard(log_mutex); 
                            std::cerr << COLOR_MAGENTA << "\n[ITEM CLOSURE] " << COLOR_RESET
                                    << "Auction-1 Item-" << (current_index + 1)
                                    << "\nWinner: " << COLOR_GREEN << winner_name << COLOR_RESET
                                    << " (ID: " << winner_id << ")"
                                    << "\nAmount: " << COLOR_GREEN << "$" << winner_amount
                                    << COLOR_RESET << "\n" << std::string(40, '=') << std::endl;
                        }
                    }
                }
                mysql_free_result(winner_res);

                int current_item_order = current_index + 1;
                std::string update_status_query = 
                "UPDATE items SET status = 'CLOSED' "
                "WHERE id = (SELECT item_id FROM auction_items "
                "WHERE auction_id = 1 AND item_order = " + std::to_string(current_item_order) + ")";

                if (mysql_query(conn, update_status_query.c_str())) {
                    throw std::runtime_error("Failed to close item: " + std::string(mysql_error(conn)));
                }

                // Update auction state
                if (current_index < total_items - 1) {
                    std::string update_auction = 
                        "UPDATE auctions SET current_item_index = " +
                        std::to_string(current_index + 1) + " WHERE id = 1";
                        execute_query(conn, update_auction);
                        std::string next_item_details = get_current_auction_details(conn); 
                        {
                            std::lock_guard<std::mutex> guard(clients_mutex);
                            for (SOCKET client : clients) {
                                send(client, next_item_details.c_str(), next_item_details.size(), 0);
                            }
                        }
                    if (mysql_query(conn, update_auction.c_str())) {
                        throw std::runtime_error("Auction update failed: " + std::string(mysql_error(conn)));
                    }

                    // Get next item name
                    std::string next_item_name = "Unknown Item";
                    std::string next_item_desc = "No description"; 
                    float next_item_base_price = 0.0f;

                    std::string next_item_query = 
                        "SELECT i.name, i.description, i.base_price "
                        "FROM items i "
                        "INNER JOIN auction_items ai ON i.id = ai.item_id "
                        "WHERE ai.auction_id = 1 AND ai.item_order = " + 
                        std::to_string(current_index + 2);

                    execute_query(conn, next_item_query);
                    MYSQL_RES* next_item_res = mysql_store_result(conn);
                    if (next_item_res && mysql_num_rows(next_item_res) > 0) {
                        MYSQL_ROW next_item_row = mysql_fetch_row(next_item_res);
                        next_item_name = next_item_row[0] ? next_item_row[0] : "Unknown Item";
                        next_item_desc = next_item_row[1] ? next_item_row[1] : "No description";
                        next_item_base_price = next_item_row[2] ? std::stof(next_item_row[2]) : 0.0f;
                    }
                    mysql_free_result(next_item_res);

                            {
                                std::lock_guard<std::mutex> log_guard(log_mutex); 
                                std::cerr << COLOR_CYAN << "[AUCTION PROGRESSION] " << COLOR_RESET
                                        << "Now accepting bids for "
                                        << BOLD << "Item-" << (current_index + 2) << COLOR_RESET
                                        << " (" << next_item_name << ")\n";
                            }

                    std::string current_item_name = "Unknown Item";
                    std::string current_item_query = "SELECT name FROM items WHERE id = " +
                                                    std::to_string(current_index + 1);
                    execute_query(conn, current_item_query);
                    MYSQL_RES* current_item_res = mysql_store_result(conn);
                    if (current_item_res && mysql_num_rows(current_item_res) > 0) {
                        MYSQL_ROW current_item_row = mysql_fetch_row(current_item_res);
                        current_item_name = current_item_row[0] ? current_item_row[0] : "Unknown Item";
                    }
                    mysql_free_result(current_item_res);
                            
                    // Client broadcast messages
                    std::string closure_msg = 
                    COLOR_MAGENTA "\n+---------------- AUCTION UPDATE ----------------+\n" COLOR_RESET
                    COLOR_CYAN " Closed Item: " COLOR_RESET "%d. %s\n"
                    COLOR_GREEN " Winner: " COLOR_RESET "%s\n"
                    COLOR_GREEN " Winning Bid: " COLOR_RESET "$%.2f\n"
                    COLOR_MAGENTA "+---------------- NEXT ITEM AVAILABLE --------------+\n" COLOR_RESET
                    COLOR_YELLOW " Item: " COLOR_RESET "%s\n"
                    COLOR_CYAN " Item ID: " COLOR_RESET "%d\n"
                    COLOR_YELLOW " Description: " COLOR_RESET "%.50s\n"  // Limit description to 50 chars
                    COLOR_GREEN " Base Price: " COLOR_RESET "$%.2f\n"
                    COLOR_MAGENTA "+---------------------------------------------+\n" COLOR_RESET;

                    char formatted_msg[512];
                    snprintf(formatted_msg, sizeof(formatted_msg), closure_msg.c_str(),
                        current_index + 1,
                        current_item_name.c_str(),
                        winner_name.c_str(),
                        std::stof(winner_amount),
                        next_item_name.c_str(),
                        current_index + 2,
                        next_item_desc.c_str(),  // NOW VALID
                        next_item_base_price);
                            {
                                std::lock_guard<std::mutex> guard(clients_mutex);
                                for (SOCKET client : clients) {
                                    send(client, formatted_msg, strlen(formatted_msg), 0);
                                }
                            }                        
                } 
                else {
                    // Final closure logging
                    {
                        std::lock_guard<std::mutex> log_guard(log_mutex);
                        std::cerr << COLOR_MAGENTA << "[AUCTION CONCLUSION] All items completed" << COLOR_RESET << std::endl;
                    }
                    std::string end_msg = "\033[35mAuction-1 has concluded! Thank you participants!\033[0m";
                    {
                        std::lock_guard<std::mutex> guard(clients_mutex);
                        for (SOCKET client : clients) {
                            send(client, end_msg.c_str(), end_msg.size(), 0);
                        }
                    }
                    } 
        
                execute_query(conn, "COMMIT");
                send(client_socket, "ADMIN_ACTION_COMPLETE", 21, 0);
            }
            catch (const std::exception& e) {
                execute_query(conn, "ROLLBACK");
                {
                    std::lock_guard<std::mutex> log_guard(log_mutex);
                    std::cerr << COLOR_RED << "[ADMIN ERROR] " << e.what() << COLOR_RESET << std::endl;
                }
                std::string error_msg = "ERROR: " + std::string(e.what());
                send(client_socket, error_msg.c_str(), error_msg.size(), 0);
            }
        }
        
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_info.address.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cerr << COLOR_RED << "[DISCONNECT] " << COLOR_RESET
             << client_info.username << " (" << client_ip << ")" << std::endl;

    mysql_close(conn);
    closesocket(client_socket);
    {
        std::lock_guard<std::mutex> guard(clients_mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << COLOR_RED << "WSAStartup failed!" << COLOR_RESET << std::endl;
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    // std::cout << "Server running on port " << PORT << std::endl;
    std::cout << COLOR_GREEN << "Server running on port " << PORT << COLOR_RESET << std::endl;

    while (true) {
        sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_fd, (sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << COLOR_RED << "Accept failed: " << WSAGetLastError() << COLOR_RESET << std::endl;
            continue;
        }

        {
            std::lock_guard<std::mutex> guard(clients_mutex);
            clients.push_back(client_socket);
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        std::cerr << COLOR_GREEN << "[CONNECTION] " << COLOR_RESET 
                 << "New client from: " << client_ip << std::endl;

        ClientInfo client_info{client_socket, client_addr, ""};
        std::thread(handle_client, client_info).detach();

    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}