if exists (select * from sysdatabases where name='pubs')
begin
  raiserror('Dropping existing pubs database ....',0,1)
  DROP database pubs
end
GO

CHECKPOINT
go
