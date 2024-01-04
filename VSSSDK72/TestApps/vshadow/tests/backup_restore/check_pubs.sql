-- This script just checks the pubs DB for consistency...

use pubs

dbcc checkdb('pubs')
