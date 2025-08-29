<?php
// === CONFIGURATION ===
$BOTtoken = [
    'mb'      => 'YOUR_TELEGRAM_API_BOTTOKEN_GOES_HERE',
    'mybot'   => 'FUTURE_USE_FOR_ADDITIONAL_BOTS_FEEL_FREE_TO_DELETE',
    'default' => 'YOUR_TELEGRAM_API_BOTTOKEN_USED_FOR_DEFAULT_GOES_HERE'
];

// match API key and Bot Token
// create your own senderIDs and tokens, make sure gpstracker.c uses a valid combination
$APIkeys = [
    'mb'      => 'CREATE_YOUR_OWN_API_KEY1',
    'mybot'   => 'CREATE_YOUR_OWN_API_KEY2',
    'default' => 'CREATE_YOUR_OWN_API_KEY3'
];

// match recipient to Telegram ChatID
$recipient = [
    'ab'      => 'TELEGRAM_CHATID_OF_USER_AB',
    'cd'      => 'TELEGRAM_CHATID_OF_USER_CD'
    'default' => 'TELEGRAM_CHATID_OF_DEFAULT_USER',
];

// match locations to known safe places
$referencePoints = [
    ['lat' => 50.123456, 'lon' => 5.678901, 'label' => "home"],
    ['lat' => 49.654321, 'lon' => 6.543210, 'label' => "marina"],
    ['lat' => 48.484848, 'lon' => 7.878787, 'label' => "winter storage"]
];

// define the max. distance from the geo location deemed as "is there" in meters
// value must be < 999, otherwise you have to modify the m/km differentiation below
define('DISTANCE_THRESHOLD', 250);

// === DETERMINE BOTTOKEN FROM PARAMETERS ===
$botKey = isset($_GET['b']) && isset($BOTtoken[$_GET['b']]) ? $_GET['b'] : 'default';
$botToken = $BOTtoken[$botKey];

// === API-KEY CHECK ===
if (!isset($_GET['k']) || $_GET['k'] !== $APIkeys[$botKey]) {
    header('Content-Type: application/json', true, 403);
    echo json_encode([
        'status' => 'error',
        'error'  => 'Invalid API key for selected bot.'
    ]);
    exit;
}

// === DETERMINE RECIPIENT ===
$recKey = isset($_GET['r']) && isset($recipient[$_GET['r']]) ? $_GET['r'] : 'default';
$chatId = $recipient[$recKey];

// === CALCULATE DISTANCE (Haversine) ===
function distanceMeters($lat1, $lon1, $lat2, $lon2) {
    $R = 6371000.0; // earth radius [meters]
    $dLat = deg2rad($lat2 - $lat1);
    $dLon = deg2rad($lon2 - $lon1);
    $a = sin($dLat / 2) * sin($dLat / 2) +
         cos(deg2rad($lat1)) * cos(deg2rad($lat2)) *
         sin($dLon / 2) * sin($dLon / 2);
    $c = 2 * atan2(sqrt($a), sqrt(1 - $a));
    return $R * $c;
}

// === TEMPERATURE CHECKS ===
function tempOutput($value, $label) {
    if (!is_numeric($value)) return null;
    $v = floatval($value);
    if ($v == -127) {
        return "⚠️ 1-Wire error ($label)";
    } elseif ($v == 85) {
        return "⚠️ sensor error ($label)";
    } else {
        return "$label {$v} °C";
    }
}

$parts = [];

$lat = null;
$lon = null;

if (isset($_GET['lat']) && isset($_GET['lon']) && is_numeric($_GET['lat']) && is_numeric($_GET['lon'])) {
    $lat = floatval($_GET['lat']);
    $lon = floatval($_GET['lon']);
    
    if ($lon * $lat > 0) {
        $mapLink = "https://maps.google.com/?q=$lat,$lon";
        $minDist = null;
        $minLabel = "";
        foreach ($referencePoints as $ref) {
            $dist = distanceMeters($lat, $lon, $ref['lat'], $ref['lon']);
            if ($minDist === null || $dist < $minDist) {
                $minDist = $dist;
                $minLabel = $ref['label'];
            }
        }
        if ($minDist > DISTANCE_THRESHOLD) {
            $parts[] = "🌍 <a href=\"$mapLink\">location: $lat, $lon</a>";
            if ($minDist > 999) {
                $parts[] = sprintf("🚨 %.1f km distance to ", $minDist / 1000) . $minLabel;
            } else {
                $parts[] = sprintf("🚨 %.0f m distance to ", $minDist) . $minLabel;
            }
        } else {
            $parts[] = "🌍 position: <a href=\"$mapLink\">$minLabel</a>";
        }
    } else {
        $parts[] = "🚨 no valid GPS position";				
    }
}

if (isset($_GET['t2'])) {
    $out = tempOutput($_GET['t2'], "🌬 air");
    if ($out) $parts[] = $out;
}
if (isset($_GET['t3'])) {
    $out = tempOutput($_GET['t3'], "💧 water");
    if ($out) $parts[] = $out;
}
if (isset($_GET['t1'])) {
    $out = tempOutput($_GET['t1'], "🌡 device");
    if ($out) $parts[] = $out;
}

// === GET WEATHER FORECAST ===
if ($lat !== null && $lon !== null) {
    // your API key goes here
    $owmApiKey = "YOUR_OPENWEATHERMAP.ORG_API_KEY_GOES_HERE";

    // One Call API 3.0 (day & night temperatures)
    $owmUrl = "https://api.openweathermap.org/data/3.0/onecall"
            . "?lat={$lat}&lon={$lon}"
            . "&exclude=minutely,hourly,alerts"
            . "&appid={$owmApiKey}"
            . "&units=metric&lang=en";

    $weatherJson = @file_get_contents($owmUrl);

    if ($weatherJson !== false) {
        $weatherData = json_decode($weatherJson, true);

        if (isset($weatherData['daily']) && is_array($weatherData['daily'])) {
            $forecastParts = [];

            // English day shorts → German, if you need. Remember to use $weekdayDe then later.
            // $weekdayMap = [
            //    'Mon' => 'Mo', 'Tue' => 'Di', 'Wed' => 'Mi',
            //    'Thu' => 'Do', 'Fri' => 'Fr', 'Sat' => 'Sa', 'Sun' => 'So'
            // ];

            foreach ($weatherData['daily'] as $day) {
                $weekdayEn = strftime('%a', $day['dt']);
                $weekdayDe = $weekdayMap[$weekdayEn] ?? $weekdayEn;

                if (in_array($weekdayEn, ['Fri', 'Sat', 'Sun'])) {
                    $desc      = ucfirst($day['weather'][0]['description']);
                    $tempDay   = round($day['temp']['day']);
                    $tempNight = round($day['temp']['night']);
                    $forecastParts[] = "$weekdayDe: $desc, {$tempDay}°C ☀️ / {$tempNight}°C 🌙";
                }
            }

            if (!empty($forecastParts)) {
                $parts[] = "\n🌤️ weather:\n" . implode("\n", $forecastParts);
            } else {
                $parts[] = "\n🌤️ weather: no valid forecast.";
            }
        } else {
            $parts[] = "\n🌤️ weather: no data available.";
        }
    } else {
        $parts[] = "\n🌤️ weather: API error.";
    }
}

if (!empty($parts)) {
    $message = "Heartbeat:\n" . implode("\n", $parts) . "\n";
    $telegramUrl = "https://api.telegram.org/bot$botToken/sendMessage";
    $data = [
        'chat_id' => $chatId,
        'text' => $message,
        'parse_mode' => 'HTML'
    ];
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $telegramUrl);
    curl_setopt($ch, CURLOPT_POST, 1);
    curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($data));
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);

    header('Content-Type: application/json');
    echo json_encode([
        'status' => $httpCode == 200 ? 'ok' : 'error',
    ]);
}
?>
