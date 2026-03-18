<?php
/**
 * Handler script for specific URIs routed by .htaccess
 */

// Return JSON response for easy parsing by tests and clients
header('Content-Type: application/json');

$response = [
    'status' => 'success',
    'handled_by' => 'index.php',
    'original_uri' => $_SERVER['REQUEST_URI'] ?? '',
    'query_string' => $_SERVER['QUERY_STRING'] ?? '',
    'method' => $_SERVER['REQUEST_METHOD'] ?? '',
    'timestamp' => time(),
];

echo json_encode($response, JSON_PRETTY_PRINT);
