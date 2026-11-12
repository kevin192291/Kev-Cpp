---
description: Logging set up once at start-up, a logger per module, and debug messages when verbose
order: 31
section: system
---
import logging

log = logging.getLogger(__name__)


# Call once, at the start of main(); other modules only need log = logging.getLogger(__name__).
# Arguments are formatted only when the message is shown: log.info("loaded %d rows from %s", count, path)
def setup_logging(verbose: bool = False) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )
