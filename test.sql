-- Load the extension
select load_extension('libvtable.so', 'fs_register');

-- Pretty printing
.h on
.m column
.w 5 5 5 9 5 100

-- Create the virtual table
create virtual table fs using filesystem;

-- Look for stuff
select 
  prot, uid, gid, size/(1024*1024) as 'size (Mb)', 
  dev, path || '/' || name as file 
from fs 
where 
    path match '/usr/lib, /var/log'
    order by size desc;

-- Note the order by will cause results to be written to a temp table and
-- ordered before printing results

select sum(size/(1024.0*1024.0*1024.0)) as 'size (GB)' 
from fs 
where
  path match '/usr/lib,/var/log';
  