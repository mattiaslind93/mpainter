<?php
// Receives a report from the application's Help ▸ Report Bug box and mails it on.
//
// The destination address is deliberately not in this file, so that publishing
// this repository never publishes the address. Provide it on the server in one
// of two ways:
//   * the environment variable MPAINTER_REPORT_TO, or
//   * a file named report-to.txt beside this script, holding just the address.
// Upload this file as /mpainter/report.php; the application posts to it.

declare(strict_types=1);

function destination(): string
{
    $value = getenv('MPAINTER_REPORT_TO');
    if (is_string($value) && $value !== '') {
        return trim($value);
    }
    $path = __DIR__ . '/report-to.txt';
    return is_readable($path) ? trim((string)file_get_contents($path)) : '';
}

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    http_response_code(405);
    exit('POST only');
}

$message = trim((string)($_POST['message'] ?? ''));
$version = trim((string)($_POST['version'] ?? 'unknown'));
$system = trim((string)($_POST['system'] ?? 'unknown'));

// A report is a person writing a paragraph. Anything outside that is not one.
if ($message === '' || strlen($message) > 20000) {
    http_response_code(400);
    exit('Empty or oversized report');
}

// One report per address every 20 seconds is plenty for a person and stops a
// script from using this as a relay.
$stamp = sys_get_temp_dir() . '/mpainter-report-' . sha1((string)($_SERVER['REMOTE_ADDR'] ?? ''));
if (is_file($stamp) && time() - (int)filemtime($stamp) < 20) {
    http_response_code(429);
    exit('Too many reports');
}
touch($stamp);

$to = destination();
if ($to === '') {
    error_log('mPainter report: no destination configured');
    http_response_code(500);
    exit('Not configured');
}

// Header injection is the one real risk here, and newlines are how it is done.
$clean = static fn(string $value): string => trim(str_replace(["\r", "\n"], ' ', $value));

$body = "mPainter version: " . $clean($version) . "\n"
      . "System: " . $clean($system) . "\n"
      . "Received: " . gmdate('Y-m-d H:i:s') . " UTC\n\n"
      . $message . "\n";

$headers = "From: mPainter reports <noreply@bluedino.se>\r\n"
         . "Content-Type: text/plain; charset=UTF-8\r\n";

if (!mail($to, 'mpainter Bugreport', $body, $headers)) {
    error_log('mPainter report: mail() failed');
    http_response_code(500);
    exit('Could not send');
}

http_response_code(200);
echo 'ok';
