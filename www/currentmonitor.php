<!DOCTYPE html>
<html>
<head>
<?php
require_once 'config.php';
require_once 'common.php';
include 'common/menuHead.inc';
?>
<script>


function OnEnableClicked(name) {
    $.get("api/command/Set Port Status/" +encodeURIComponent(name) + "/true");
}
function OnDisableClicked(name) {
    $.get("api/command/Set Port Status/" + encodeURIComponent(name) + "/false");
}

function CountPixels() {
    $.get("api/fppd/ports/pixelCount");
}

function FormatSmartReceiver(name, k, port, row, col) {
    if (port == null) {
        return "";
    }
    var html = "";
    if (name != "") {
        html += "<b>" + name + "</b><br>";
    }
    html += "<b>" + k + ":</b><br>";
    html += "<div style='margin-left: 15px;'>";

    if (port["enabled"]) {
        html += "Enabled: <i class='fas fa-check-circle text-success' title='Port Enabled' onclick='OnDisableClicked(\"" + name + k + "\")'></i><br>";
    } else if (port["status"]) {
        html += "Enabled: <i class='fas fa-times-circle text-info' title='Port Disabled' onclick='OnEnableClicked(\"" + name + k + "\")'></i><br>";
    } else {
        html += "Enabled: <i class='fas fa-times-circle text-danger' title='eFuse Triggered' onclick='OnEnableClicked(\"" + name + k + "\")'></i><br>";
    }
    if (port["status"]) {
        html += "Status: <i class='fas fa-check-circle text-success' title='eFuse Normal'></i><br>";
    } else {
        html += "Status: <i class='fas fa-times-circle text-danger' title='eFuse Triggered'></i><br>";
    }
    if (typeof port.pixelCount !== 'undefined') {
        html += "Pixels: " + port["pixelCount"] + "<br>";
    }
    html += port["ma"] + " ma";

    html += "</div>";
    $("#fppPorts tr:nth-child(" + row + ") td:nth-child(" + col + ")").html(html);
    $("#fppPorts tr:nth-child(" + row + ")").show();
}
function StartMonitoring() {
    $.get('api/fppd/ports', function(data) {
        data.forEach(function(port) {
            var count = $('#fppPorts tr').length;
            var rn = port["row"];
            if (port["smartReceivers"]) {
                rn += 6;
            }
            while (rn >= count) {
                $('#fppPorts').append("<tr style='display:none'><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td></tr>");
                count++;
            }
            if (!port["smartReceivers"]) {
                $("#fppPorts tr:nth-child(" + port["row"] + ")").show();
                var html = "<b>" + port["name"] + "</b><br>";
                if (port["enabled"]) {
                    html += "Enabled: <i class='fas fa-check-circle text-success' title='Port Enabled' onclick='OnDisableClicked(\"" + port["name"] + "\")'></i><br>";
                } else if (port["status"]) {
                    html += "Enabled: <i class='fas fa-times-circle text-info' title='Port Disabled' onclick='OnEnableClicked(\"" + port["name"] + "\")'></i><br>";
                } else {
                    html += "Enabled: <i class='fas fa-times-circle text-danger' title='eFuse Triggered' onclick='OnEnableClicked(\"" + port["name"] + "\")'></i><br>";
                }
                if (port["status"]) {
                    html += "Status: <i class='fas fa-check-circle text-success' title='eFuse Normal'></i><br>";
                } else {
                    html += "Status: <i class='fas fa-times-circle text-danger' title='eFuse Triggered'></i><br>";
                }
                if (typeof port.pixelCount !== 'undefined') {
                    html += "Pixels: " + port["pixelCount"] + "<br>";
                }
                html += port["ma"] + " ma";
                $("#fppPorts tr:nth-child(" + port["row"] + ") td:nth-child(" + port["col"] + ")").html(html);
            } else {
                rn = port["row"];
                FormatSmartReceiver(port["name"], "A", port["A"], rn, port["col"]);
                FormatSmartReceiver("", "B", port["B"], rn + 1, port["col"]);
                FormatSmartReceiver("", "C", port["C"], rn + 2, port["col"]);
                FormatSmartReceiver("", "D", port["D"], rn + 3, port["col"]);
                FormatSmartReceiver("", "E", port["E"], rn + 4, port["col"]);
                FormatSmartReceiver("", "F", port["F"], rn + 5, port["col"]);
            }
        });
    });
}
function StopPixelCount() {
    $.get("api/fppd/ports/stop");
}

function SetupPage() {
    setInterval(StartMonitoring, 1000);
    window.addEventListener('beforeunload', StopPixelCount, false);
}
</script>

<title><?echo $pageTitle; ?></title>
</head>
<body class="is-loading" onLoad="SetupPage();">
<div id="bodyWrapper">
<?php
$activeParentMenuItem = 'status';
include 'menu.inc';?>
  <div class="mainContainer">
    <h2 class="title d-none d-sm-block ">FPP Current Monitor</h2>
    <div class="pageContent">
        <div id='fppSystemsTableWrapper' class='fppTableWrapper fppTableWrapperAsTable backdrop'>
            <div class='fppTableContents' role="region" aria-labelledby="fppSystemsTable" tabindex="0">
        		<table id='fppPortsTable' cellpadding='4' width="100%">
                    <tbody id='fppPorts' width="100%">
                        <tr><td width="12%"></td><td width="12%"></td><td width="12%"></td><td width="12%"></td><td width="12%"></td><td width="12%"></td><td width="12%"></td><td width="12%"></td></tr>
                    </tbody>
                </table>
            </div>
        </div>
        <input id="btnCountPixels" class="buttons" type="button" value = "Count Pixels" onClick="CountPixels();" />
    </div>
  </div>
<?php	include 'common/footer.inc';?>
</div>
</body>
</html>
