# Site endpoint

`report.php` receives the Help ▸ Report Bug box from the application and mails
the text on with the subject `mpainter Bugreport`.

Upload it to the product site as `/mpainter/report.php`, so that it answers at
the address the application posts to:

    https://bluedino.se/mpainter/report.php

Then give it a destination, either by setting `MPAINTER_REPORT_TO` in the server
environment or by placing the address in `report-to.txt` beside the script. The
address is kept out of this repository on purpose: it is not in the application
either, so it cannot be read out of the binary or seen by anyone reporting.
