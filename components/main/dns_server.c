#include "dns_server.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "dns_server.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <string.h>

static const char *TAG = "dns_server";
static int dns_server_socket = -1;
static bool dns_server_running = false;

// DNS-Header Struktur
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

// DNS-Question Struktur
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS-Response Header erstellen
static void create_dns_response(uint8_t *response, uint16_t *response_len, 
                                 const uint8_t *request, uint16_t request_len,
                                 const char *redirect_ip) {
    dns_header_t *req_header = (dns_header_t *)request;
    dns_header_t *resp_header = (dns_header_t *)response;
    
    // Header kopieren
    memcpy(response, request, sizeof(dns_header_t));
    
    // Flags setzen: Response, Recursion Available
    resp_header->flags = htons(0x8180);
    resp_header->qdcount = req_header->qdcount;
    resp_header->ancount = htons(1);  // Eine Antwort
    resp_header->nscount = 0;
    resp_header->arcount = 0;
    
    uint16_t offset = sizeof(dns_header_t);
    
    // Question kopieren
    memcpy(response + offset, request + offset, request_len - sizeof(dns_header_t));
    offset += (request_len - sizeof(dns_header_t));
    
    // Answer erstellen
    // Name Pointer (0xC00C = Verweis auf Question Name)
    uint16_t name_ptr = htons(0xC00C);
    memcpy(response + offset, &name_ptr, 2);
    offset += 2;
    
    // Type A (IPv4)
    uint16_t type_a = htons(1);
    memcpy(response + offset, &type_a, 2);
    offset += 2;
    
    // Class IN
    uint16_t class_in = htons(1);
    memcpy(response + offset, &class_in, 2);
    offset += 2;
    
    // TTL (300 Sekunden)
    uint32_t ttl = htonl(300);
    memcpy(response + offset, &ttl, 4);
    offset += 4;
    
    // Data Length (4 Bytes für IPv4)
    uint16_t data_len = htons(4);
    memcpy(response + offset, &data_len, 2);
    offset += 2;
    
    // IP-Adresse (Redirect IP)
    uint8_t ip_bytes[4];
    sscanf(redirect_ip, "%hhu.%hhu.%hhu.%hhu", &ip_bytes[0], &ip_bytes[1], &ip_bytes[2], &ip_bytes[3]);
    memcpy(response + offset, ip_bytes, 4);
    offset += 4;
    
    *response_len = offset;
}

// DNS-Server Task
static void dns_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "DNS-Server Task gestartet");
    
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    uint8_t request_buffer[512];
    uint8_t response_buffer[512];
    
    while (dns_server_running) {
        int recv_len = recvfrom(dns_server_socket, request_buffer, sizeof(request_buffer), 0,
                                (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (recv_len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "recvfrom fehlgeschlagen: %d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        if (recv_len < sizeof(dns_header_t)) {
            ESP_LOGW(TAG, "DNS-Paket zu kurz: %d Bytes", recv_len);
            continue;
        }
        
        dns_header_t *header = (dns_header_t *)request_buffer;
        uint16_t flags = ntohs(header->flags);
        
        // Nur Queries verarbeiten (QR=0)
        if ((flags & 0x8000) != 0) {
            continue;
        }
        
        // DNS-Response erstellen (alle Anfragen auf AP-IP weiterleiten)
        uint16_t response_len;
        create_dns_response(response_buffer, &response_len, request_buffer, recv_len, WIFI_AP_IP);
        
        // Response senden
        int sent_len = sendto(dns_server_socket, response_buffer, response_len, 0,
                              (struct sockaddr *)&client_addr, client_addr_len);
        
        if (sent_len < 0) {
            ESP_LOGE(TAG, "sendto fehlgeschlagen: %d", errno);
        }
    }
    
    ESP_LOGI(TAG, "DNS-Server Task beendet");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void) {
    if (dns_server_running) {
        ESP_LOGW(TAG, "DNS-Server läuft bereits");
        return ESP_OK;
    }
    
    // UDP Socket erstellen
    dns_server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_server_socket < 0) {
        ESP_LOGE(TAG, "Socket-Erstellung fehlgeschlagen: %d", errno);
        return ESP_FAIL;
    }
    
    // Socket konfigurieren
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);  // DNS Port
    
    // Socket binden
    if (bind(dns_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind fehlgeschlagen: %d", errno);
        close(dns_server_socket);
        dns_server_socket = -1;
        return ESP_FAIL;
    }
    
    // Non-blocking Mode
    int flags = fcntl(dns_server_socket, F_GETFL, 0);
    fcntl(dns_server_socket, F_SETFL, flags | O_NONBLOCK);
    
    dns_server_running = true;
    
    // DNS-Server Task starten
    BaseType_t ret = xTaskCreatePinnedToCore(
        dns_server_task,
        "dns_server",
        4096,
        NULL,
        5,
        NULL,
        TASK_CORE_NETWORK
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "DNS-Server Task-Erstellung fehlgeschlagen");
        close(dns_server_socket);
        dns_server_socket = -1;
        dns_server_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "DNS-Server gestartet auf Port 53");
    return ESP_OK;
}

esp_err_t dns_server_stop(void) {
    if (!dns_server_running) {
        return ESP_OK;
    }
    
    dns_server_running = false;
    
    if (dns_server_socket >= 0) {
        close(dns_server_socket);
        dns_server_socket = -1;
    }
    
    ESP_LOGI(TAG, "DNS-Server gestoppt");
    return ESP_OK;
}
