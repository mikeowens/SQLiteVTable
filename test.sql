-- Load the extension
select load_extension('libvtable.so', 'lib_init');

-- Pretty printing
.h on
.m col
.w 5 5 5 9 5 100

-- Create the virtual table
create virtual table fs using filesystem;

-- Look for stuff
select 
  prot, uid, gid, size/(1024*1024) as 'size (Mb)', 
  dev, path || '/' || name as file from fs 
where 
    path match '/usr/lib, /var/log'
    AND size > 1024*1024*4 ;

-- Note the order by will cause results to be written to a temp table and
-- ordered before printing results

-- ORDER BY size desc;  
