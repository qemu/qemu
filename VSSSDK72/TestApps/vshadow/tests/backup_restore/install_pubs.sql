/*                                                                        */
/*              InstPubs.SQL - Creates the Pubs database                  */ 
/*                                                                        */
/*
** Copyright Microsoft, Inc. 1994 - 2000
** All Rights Reserved.
*/

GO

set nocount    on
set dateformat mdy

USE master

declare @dttm nvarchar(55)
select  @dttm=convert(nvarchar,getdate(),113)
raiserror('Beginning InstPubs.SQL at %s ....',1,1,@dttm) with nowait

GO

/*

raiserror('Creating pubs database....',0,1)
go

   If SQL Server 4.2, 6.0, or 6.5, create a 3MB database.
   Use default size with autogrow if SQL Server 7.0 or later.

IF (CHARINDEX('4.2', @@version) > 0 OR
    CHARINDEX('6.00', @@version) > 0 OR
    CHARINDEX('6.50', @@version) > 0 )

   CREATE DATABASE pubs ON DEFAULT = 3
ELSE
   CREATE DATABASE pubs
GO

CHECKPOINT

GO

*/


USE pubs

GO

if db_name() <> 'pubs'
   raiserror('Error in InstPubs.SQL, ''USE pubs'' failed!  Killing the SPID now.'
            ,22,127) with log

GO

execute sp_dboption 'pubs' ,'trunc. log on chkpt.' ,'true'

execute sp_addtype id      ,'nvarchar(11)' ,'NOT NULL'
execute sp_addtype tid     ,'nvarchar(6)'  ,'NOT NULL'
execute sp_addtype empid   ,'nchar(9)'     ,'NOT NULL'

raiserror('Now at the create table section ....',0,1)

GO

CREATE TABLE authors
(
   au_id          id

         CHECK (au_id like '[0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9][0-9][0-9]')

         CONSTRAINT UPKCL_auidind PRIMARY KEY CLUSTERED,

   au_lname       nvarchar(40)       NOT NULL,
   au_fname       nvarchar(20)       NOT NULL,

   phone          nchar(12)          NOT NULL

         DEFAULT ('UNKNOWN'),

   address        nvarchar(40)           NULL,
   city           nvarchar(20)           NULL,
   state          nchar(2)               NULL,

   zip            nchar(5)               NULL

         CHECK (zip like '[0-9][0-9][0-9][0-9][0-9]'),

   contract       bit               NOT NULL
)

GO

CREATE TABLE publishers
(
   pub_id         nchar(4)           NOT NULL

         CONSTRAINT UPKCL_pubind PRIMARY KEY CLUSTERED

         CHECK (pub_id in ('1389', '0736', '0877', '1622', '1756')
            OR pub_id like '99[0-9][0-9]'),

   pub_name       nvarchar(40)           NULL,
   city           nvarchar(20)           NULL,
   state          nchar(2)               NULL,

   country        nvarchar(30)           NULL

         DEFAULT('USA')
)

GO

CREATE TABLE titles
(
   title_id       tid

         CONSTRAINT UPKCL_titleidind PRIMARY KEY CLUSTERED,

   title          nvarchar(80)       NOT NULL,

   type           nchar(12)          NOT NULL

         DEFAULT ('UNDECIDED'),

   pub_id         nchar(4)               NULL

         REFERENCES publishers(pub_id),

   price          money                 NULL,
   advance        money                 NULL,
   royalty        int                   NULL,
   ytd_sales      int                   NULL,
   notes          nvarchar(200)          NULL,

   pubdate        datetime          NOT NULL

         DEFAULT (getdate())
)

GO

CREATE TABLE titleauthor
(
   au_id          id

         REFERENCES authors(au_id),

   title_id       tid

         REFERENCES titles(title_id),

   au_ord         tinyint               NULL,
   royaltyper     int                   NULL,


   CONSTRAINT UPKCL_taind PRIMARY KEY CLUSTERED(au_id, title_id)
)

GO

CREATE TABLE stores
(
   stor_id        nchar(4)           NOT NULL

         CONSTRAINT UPK_storeid PRIMARY KEY CLUSTERED,

   stor_name      nvarchar(40)           NULL,
   stor_address   nvarchar(40)           NULL,
   city           nvarchar(20)           NULL,
   state          nchar(2)               NULL,
   zip            nchar(5)               NULL
)

GO

CREATE TABLE sales
(
   stor_id        nchar(4)           NOT NULL

         REFERENCES stores(stor_id),

   ord_num        nvarchar(20)       NOT NULL,
   ord_date       datetime          NOT NULL,
   qty            smallint          NOT NULL,
   payterms       nvarchar(12)       NOT NULL,

   title_id       tid

         REFERENCES titles(title_id),


   CONSTRAINT UPKCL_sales PRIMARY KEY CLUSTERED (stor_id, ord_num, title_id)
)

GO

CREATE TABLE roysched
(
   title_id       tid

         REFERENCES titles(title_id),

   lorange        int                   NULL,
   hirange        int                   NULL,
   royalty        int                   NULL
)

GO

CREATE TABLE discounts
(
   discounttype   nvarchar(40)       NOT NULL,

   stor_id        nchar(4) NULL

         REFERENCES stores(stor_id),

   lowqty         smallint              NULL,
   highqty        smallint              NULL,
   discount       dec(4,2)          NOT NULL
)

GO

CREATE TABLE jobs
(
   job_id         smallint          IDENTITY(1,1)

         PRIMARY KEY CLUSTERED,

   job_desc       nvarchar(50)       NOT NULL

         DEFAULT 'New Position - title not formalized yet',

   min_lvl        tinyint           NOT NULL

         CHECK (min_lvl >= 10),

   max_lvl        tinyint           NOT NULL

         CHECK (max_lvl <= 250)
)

GO

CREATE TABLE pub_info
(
   pub_id         nchar(4)           NOT NULL

         REFERENCES publishers(pub_id)

         CONSTRAINT UPKCL_pubinfo PRIMARY KEY CLUSTERED,

   logo           image                 NULL,
   pr_info        ntext                  NULL
)

GO

CREATE TABLE employee
(
   emp_id         empid

         CONSTRAINT PK_emp_id PRIMARY KEY NONCLUSTERED

         CONSTRAINT CK_emp_id CHECK (emp_id LIKE
            '[A-Z][A-Z][A-Z][1-9][0-9][0-9][0-9][0-9][FM]' or
            emp_id LIKE '[A-Z]-[A-Z][1-9][0-9][0-9][0-9][0-9][FM]'),

   fname          nvarchar(20)       NOT NULL,
   minit          nchar(1)               NULL,
   lname          nvarchar(30)       NOT NULL,

   job_id         smallint          NOT NULL

         DEFAULT 1

         REFERENCES jobs(job_id),

   job_lvl        tinyint

         DEFAULT 10,

   pub_id         nchar(4)           NOT NULL

         DEFAULT ('9952')

         REFERENCES publishers(pub_id),

   hire_date      datetime          NOT NULL

         DEFAULT (getdate())
)

GO

raiserror('Now at the create trigger section ...',0,1)

GO

CREATE TRIGGER employee_insupd
ON employee
FOR insert, UPDATE
AS
--Get the range of level for this job type from the jobs table.
declare @min_lvl tinyint,
   @max_lvl tinyint,
   @emp_lvl tinyint,
   @job_id smallint
select @min_lvl = min_lvl,
   @max_lvl = max_lvl,
   @emp_lvl = i.job_lvl,
   @job_id = i.job_id
from employee e, jobs j, inserted i
where e.emp_id = i.emp_id AND i.job_id = j.job_id
IF (@job_id = 1) and (@emp_lvl <> 10)
begin
   raiserror ('Job id 1 expects the default level of 10.',16,1)
   ROLLBACK TRANSACTION
end
ELSE
IF NOT (@emp_lvl BETWEEN @min_lvl AND @max_lvl)
begin
   raiserror ('The level for job_id:%d should be between %d and %d.',
      16, 1, @job_id, @min_lvl, @max_lvl)
   ROLLBACK TRANSACTION
end

GO

raiserror('Now at the inserts to authors ....',0,1)

GO

insert authors
   values('409-56-7008', 'Bennet', 'Abraham', '415 658-9932',
   '6223 Bateman St.', 'Berkeley', 'CA', '94705', 1)
insert authors
   values('213-46-8915', 'Green', 'Marjorie', '415 986-7020',
   '309 63rd St. #411', 'Oakland', 'CA', '94618', 1)
insert authors
   values('238-95-7766', 'Carson', 'Cheryl', '415 548-7723',
   '589 Darwin Ln.', 'Berkeley', 'CA', '94705', 1)
insert authors
   values('998-72-3567', 'Ringer', 'Albert', '801 826-0752',
   '67 Seventh Av.', 'Salt Lake City', 'UT', '84152', 1)
insert authors
   values('899-46-2035', 'Ringer', 'Anne', '801 826-0752',
   '67 Seventh Av.', 'Salt Lake City', 'UT', '84152', 1)
insert authors
   values('722-51-5454', 'DeFrance', 'Michel', '219 547-9982',
   '3 Balding Pl.', 'Gary', 'IN', '46403', 1)
insert authors
   values('807-91-6654', 'Panteley', 'Sylvia', '301 946-8853',
   '1956 Arlington Pl.', 'Rockville', 'MD', '20853', 1)
insert authors
   values('893-72-1158', 'McBadden', 'Heather',
   '707 448-4982', '301 Putnam', 'Vacaville', 'CA', '95688', 0)
insert authors
   values('724-08-9931', 'Stringer', 'Dirk', '415 843-2991',
   '5420 Telegraph Av.', 'Oakland', 'CA', '94609', 0)
insert authors
   values('274-80-9391', 'Straight', 'Dean', '415 834-2919',
   '5420 College Av.', 'Oakland', 'CA', '94609', 1)
insert authors
   values('756-30-7391', 'Karsen', 'Livia', '415 534-9219',
   '5720 McAuley St.', 'Oakland', 'CA', '94609', 1)
insert authors
   values('724-80-9391', 'MacFeather', 'Stearns', '415 354-7128',
   '44 Upland Hts.', 'Oakland', 'CA', '94612', 1)
insert authors
   values('427-17-2319', 'Dull', 'Ann', '415 836-7128',
   '3410 Blonde St.', 'Palo Alto', 'CA', '94301', 1)
insert authors
   values('672-71-3249', 'Yokomoto', 'Akiko', '415 935-4228',
   '3 Silver Ct.', 'Walnut Creek', 'CA', '94595', 1)
insert authors
   values('267-41-2394', 'O''Leary', 'Michael', '408 286-2428',
   '22 Cleveland Av. #14', 'San Jose', 'CA', '95128', 1)
insert authors
   values('472-27-2349', 'Gringlesby', 'Burt', '707 938-6445',
   'PO Box 792', 'Covelo', 'CA', '95428', 3)
insert authors
   values('527-72-3246', 'Greene', 'Morningstar', '615 297-2723',
   '22 Graybar House Rd.', 'Nashville', 'TN', '37215', 0)
insert authors
   values('172-32-1176', 'White', 'Johnson', '408 496-7223',
   '10932 Bigge Rd.', 'Menlo Park', 'CA', '94025', 1)
insert authors
   values('712-45-1867', 'del Castillo', 'Innes', '615 996-8275',
   '2286 Cram Pl. #86', 'Ann Arbor', 'MI', '48105', 1)
insert authors
   values('846-92-7186', 'Hunter', 'Sheryl', '415 836-7128',
   '3410 Blonde St.', 'Palo Alto', 'CA', '94301', 1)
insert authors
   values('486-29-1786', 'Locksley', 'Charlene', '415 585-4620',
   '18 Broadway Av.', 'San Francisco', 'CA', '94130', 1)
insert authors
   values('648-92-1872', 'Blotchet-Halls', 'Reginald', '503 745-6402',
   '55 Hillsdale Bl.', 'Corvallis', 'OR', '97330', 1)
insert authors
   values('341-22-1782', 'Smith', 'Meander', '913 843-0462',
   '10 Mississippi Dr.', 'Lawrence', 'KS', '66044', 0)

GO

raiserror('Now at the inserts to publishers ....',0,1)

GO

insert publishers values('0736', 'New Moon Books', 'Boston', 'MA', 'USA')
insert publishers values('0877', 'Binnet & Hardley', 'Washington', 'DC', 'USA')
insert publishers values('1389', 'Algodata Infosystems', 'Berkeley', 'CA', 'USA')
insert publishers values('9952', 'Scootney Books', 'New York', 'NY', 'USA')
insert publishers values('1622', 'Five Lakes Publishing', 'Chicago', 'IL', 'USA')
insert publishers values('1756', 'Ramona Publishers', 'Dallas', 'TX', 'USA')
insert publishers values('9901', 'GGG&G', 'Mnchen', NULL, 'Germany')
insert publishers values('9999', 'Lucerne Publishing', 'Paris', NULL, 'France')

GO

raiserror('Now at the inserts to pub_info ....',0,1)

GO

insert pub_info values('0736', 0xFFFFFFFF, 'None yet')
insert pub_info values('0877', 0xFFFFFFFF, 'None yet')
insert pub_info values('1389', 0xFFFFFFFF, 'None yet')
insert pub_info values('9952', 0xFFFFFFFF, 'None yet')
insert pub_info values('1622', 0xFFFFFFFF, 'None yet')
insert pub_info values('1756', 0xFFFFFFFF, 'None yet')
insert pub_info values('9901', 0xFFFFFFFF, 'None yet')
insert pub_info values('9999', 0xFFFFFFFF, 'None yet')
GO


raiserror('Now at the inserts to titles ....',0,1)

GO

insert titles values ('PC8888', 'Secrets of Silicon Valley', 'popular_comp', '1389',
$20.00, $8000.00, 10, 4095,
'Muckraking reporting on the world''s largest computer hardware and software manufacturers.',
'06/12/94')

insert titles values ('BU1032', 'The Busy Executive''s Database Guide', 'business',
'1389', $19.99, $5000.00, 10, 4095,
'An overview of available database systems with emphasis on common business applications. Illustrated.',
'06/12/91')

insert titles values ('PS7777', 'Emotional Security: A New Algorithm', 'psychology',
'0736', $7.99, $4000.00, 10, 3336,
'Protecting yourself and your loved ones from undue emotional stress in the modern world. Use of computer and nutritional aids emphasized.',
'06/12/91')

insert titles values ('PS3333', 'Prolonged Data Deprivation: Four Case Studies',
'psychology', '0736', $19.99, $2000.00, 10, 4072,
'What happens when the data runs dry?  Searching evaluations of information-shortage effects.',
'06/12/91')

insert titles values ('BU1111', 'Cooking with Computers: Surreptitious Balance Sheets',
'business', '1389', $11.95, $5000.00, 10, 3876,
'Helpful hints on how to use your electronic resources to the best advantage.',
'06/09/91')

insert titles values ('MC2222', 'Silicon Valley Gastronomic Treats', 'mod_cook', '0877',
$19.99, $0.00, 12, 2032,
'Favorite recipes for quick, easy, and elegant meals.',
'06/09/91')

insert titles values ('TC7777', 'Sushi, Anyone?', 'trad_cook', '0877', $14.99, $8000.00,
10, 4095,
'Detailed instructions on how to make authentic Japanese sushi in your spare time.',
'06/12/91')

insert titles values ('TC4203', 'Fifty Years in Buckingham Palace Kitchens', 'trad_cook',
'0877', $11.95, $4000.00, 14, 15096,
'More anecdotes from the Queen''s favorite cook describing life among English royalty. Recipes, techniques, tender vignettes.',
'06/12/91')

insert titles values ('PC1035', 'But Is It User Friendly?', 'popular_comp', '1389',
$22.95, $7000.00, 16, 8780,
'A survey of software for the naive user, focusing on the ''friendliness'' of each.',
'06/30/91')

insert titles values('BU2075', 'You Can Combat Computer Stress!', 'business', '0736',
$2.99, $10125.00, 24, 18722,
'The latest medical and psychological techniques for living with the electronic office. Easy-to-understand explanations.',
'06/30/91')

insert titles values('PS2091', 'Is Anger the Enemy?', 'psychology', '0736', $10.95,
$2275.00, 12, 2045,
'Carefully researched study of the effects of strong emotions on the body. Metabolic charts included.',
'06/15/91')

insert titles values('PS2106', 'Life Without Fear', 'psychology', '0736', $7.00, $6000.00,
10, 111,
'New exercise, meditation, and nutritional techniques that can reduce the shock of daily interactions. Popular audience. Sample menus included, exercise video available separately.',
'10/05/91')

insert titles values('MC3021', 'The Gourmet Microwave', 'mod_cook', '0877', $2.99,
$15000.00, 24, 22246,
'Traditional French gourmet recipes adapted for modern microwave cooking.',
'06/18/91')

insert titles values('TC3218', 'Onions, Leeks, and Garlic: Cooking Secrets of the Mediterranean',
'trad_cook', '0877', $20.95, $7000.00, 10, 375,
'Profusely illustrated in color, this makes a wonderful gift book for a cuisine-oriented friend.',
'10/21/91')

insert titles (title_id, title, pub_id) values('MC3026',
'The Psychology of Computer Cooking', '0877')

insert titles values ('BU7832', 'Straight Talk About Computers', 'business', '1389',
$19.99, $5000.00, 10, 4095,
'Annotated analysis of what computers can do for you: a no-hype guide for the critical user.',
'06/22/91')

insert titles values('PS1372', 'Computer Phobic AND Non-Phobic Individuals: Behavior Variations',
'psychology', '0877', $21.59, $7000.00, 10, 375,
'A must for the specialist, this book examines the difference between those who hate and fear computers and those who don''t.',
'10/21/91')

insert titles (title_id, title, type, pub_id, notes) values('PC9999', 'Net Etiquette',
'popular_comp', '1389', 'A must-read for computer conferencing.')

GO

raiserror('Now at the inserts to titleauthor ....',0,1)

GO

insert titleauthor values('409-56-7008', 'BU1032', 1, 60)
insert titleauthor values('486-29-1786', 'PS7777', 1, 100)
insert titleauthor values('486-29-1786', 'PC9999', 1, 100)
insert titleauthor values('712-45-1867', 'MC2222', 1, 100)
insert titleauthor values('172-32-1176', 'PS3333', 1, 100)
insert titleauthor values('213-46-8915', 'BU1032', 2, 40)
insert titleauthor values('238-95-7766', 'PC1035', 1, 100)
insert titleauthor values('213-46-8915', 'BU2075', 1, 100)
insert titleauthor values('998-72-3567', 'PS2091', 1, 50)
insert titleauthor values('899-46-2035', 'PS2091', 2, 50)
insert titleauthor values('998-72-3567', 'PS2106', 1, 100)
insert titleauthor values('722-51-5454', 'MC3021', 1, 75)
insert titleauthor values('899-46-2035', 'MC3021', 2, 25)
insert titleauthor values('807-91-6654', 'TC3218', 1, 100)
insert titleauthor values('274-80-9391', 'BU7832', 1, 100)
insert titleauthor values('427-17-2319', 'PC8888', 1, 50)
insert titleauthor values('846-92-7186', 'PC8888', 2, 50)
insert titleauthor values('756-30-7391', 'PS1372', 1, 75)
insert titleauthor values('724-80-9391', 'PS1372', 2, 25)
insert titleauthor values('724-80-9391', 'BU1111', 1, 60)
insert titleauthor values('267-41-2394', 'BU1111', 2, 40)
insert titleauthor values('672-71-3249', 'TC7777', 1, 40)
insert titleauthor values('267-41-2394', 'TC7777', 2, 30)
insert titleauthor values('472-27-2349', 'TC7777', 3, 30)
insert titleauthor values('648-92-1872', 'TC4203', 1, 100)

GO

raiserror('Now at the inserts to stores ....',0,1)

GO

insert stores values('7066','Barnum''s','567 Pasadena Ave.','Tustin','CA','92789')
insert stores values('7067','News & Brews','577 First St.','Los Gatos','CA','96745')
insert stores values('7131','Doc-U-Mat: Quality Laundry and Books',
      '24-A Avogadro Way','Remulade','WA','98014')
insert stores values('8042','Bookbeat','679 Carson St.','Portland','OR','89076')
insert stores values('6380','Eric the Read Books','788 Catamaugus Ave.',
      'Seattle','WA','98056')
insert stores values('7896','Fricative Bookshop','89 Madison St.','Fremont','CA','90019')

GO

raiserror('Now at the inserts to sales ....',0,1)

GO

insert sales values('7066', 'QA7442.3', '09/13/94', 75, 'ON invoice','PS2091')
insert sales values('7067', 'D4482', '09/14/94', 10, 'Net 60','PS2091')
insert sales values('7131', 'N914008', '09/14/94', 20, 'Net 30','PS2091')
insert sales values('7131', 'N914014', '09/14/94', 25, 'Net 30','MC3021')
insert sales values('8042', '423LL922', '09/14/94', 15, 'ON invoice','MC3021')
insert sales values('8042', '423LL930', '09/14/94', 10, 'ON invoice','BU1032')
insert sales values('6380', '722a', '09/13/94', 3, 'Net 60','PS2091')
insert sales values('6380', '6871', '09/14/94', 5, 'Net 60','BU1032')
insert sales values('8042','P723', '03/11/93', 25, 'Net 30', 'BU1111')
insert sales values('7896','X999', '02/21/93', 35, 'ON invoice', 'BU2075')
insert sales values('7896','QQ2299', '10/28/93', 15, 'Net 60', 'BU7832')
insert sales values('7896','TQ456', '12/12/93', 10, 'Net 60', 'MC2222')
insert sales values('8042','QA879.1', '5/22/93', 30, 'Net 30', 'PC1035')
insert sales values('7066','A2976', '5/24/93', 50, 'Net 30', 'PC8888')
insert sales values('7131','P3087a', '5/29/93', 20, 'Net 60', 'PS1372')
insert sales values('7131','P3087a', '5/29/93', 25, 'Net 60', 'PS2106')
insert sales values('7131','P3087a', '5/29/93', 15, 'Net 60', 'PS3333')
insert sales values('7131','P3087a', '5/29/93', 25, 'Net 60', 'PS7777')
insert sales values('7067','P2121', '6/15/92', 40, 'Net 30', 'TC3218')
insert sales values('7067','P2121', '6/15/92', 20, 'Net 30', 'TC4203')
insert sales values('7067','P2121', '6/15/92', 20, 'Net 30', 'TC7777')

GO

raiserror('Now at the inserts to roysched ....',0,1)

GO

insert roysched values('BU1032', 0, 5000, 10)
insert roysched values('BU1032', 5001, 50000, 12)
insert roysched values('PC1035', 0, 2000, 10)
insert roysched values('PC1035', 2001, 3000, 12)
insert roysched values('PC1035', 3001, 4000, 14)
insert roysched values('PC1035', 4001, 10000, 16)
insert roysched values('PC1035', 10001, 50000, 18)
insert roysched values('BU2075', 0, 1000, 10)
insert roysched values('BU2075', 1001, 3000, 12)
insert roysched values('BU2075', 3001, 5000, 14)

GO

insert roysched values('BU2075', 5001, 7000, 16)
insert roysched values('BU2075', 7001, 10000, 18)
insert roysched values('BU2075', 10001, 12000, 20)
insert roysched values('BU2075', 12001, 14000, 22)
insert roysched values('BU2075', 14001, 50000, 24)
insert roysched values('PS2091', 0, 1000, 10)
insert roysched values('PS2091', 1001, 5000, 12)
insert roysched values('PS2091', 5001, 10000, 14)
insert roysched values('PS2091', 10001, 50000, 16)
insert roysched values('PS2106', 0, 2000, 10)

GO

insert roysched values('PS2106', 2001, 5000, 12)
insert roysched values('PS2106', 5001, 10000, 14)
insert roysched values('PS2106', 10001, 50000, 16)
insert roysched values('MC3021', 0, 1000, 10)
insert roysched values('MC3021', 1001, 2000, 12)
insert roysched values('MC3021', 2001, 4000, 14)
insert roysched values('MC3021', 4001, 6000, 16)
insert roysched values('MC3021', 6001, 8000, 18)
insert roysched values('MC3021', 8001, 10000, 20)
insert roysched values('MC3021', 10001, 12000, 22)

GO

insert roysched values('MC3021', 12001, 50000, 24)
insert roysched values('TC3218', 0, 2000, 10)
insert roysched values('TC3218', 2001, 4000, 12)
insert roysched values('TC3218', 4001, 6000, 14)
insert roysched values('TC3218', 6001, 8000, 16)
insert roysched values('TC3218', 8001, 10000, 18)
insert roysched values('TC3218', 10001, 12000, 20)
insert roysched values('TC3218', 12001, 14000, 22)
insert roysched values('TC3218', 14001, 50000, 24)
insert roysched values('PC8888', 0, 5000, 10)
insert roysched values('PC8888', 5001, 10000, 12)

GO

insert roysched values('PC8888', 10001, 15000, 14)
insert roysched values('PC8888', 15001, 50000, 16)
insert roysched values('PS7777', 0, 5000, 10)
insert roysched values('PS7777', 5001, 50000, 12)
insert roysched values('PS3333', 0, 5000, 10)
insert roysched values('PS3333', 5001, 10000, 12)
insert roysched values('PS3333', 10001, 15000, 14)
insert roysched values('PS3333', 15001, 50000, 16)
insert roysched values('BU1111', 0, 4000, 10)
insert roysched values('BU1111', 4001, 8000, 12)
insert roysched values('BU1111', 8001, 10000, 14)

GO

insert roysched values('BU1111', 12001, 16000, 16)
insert roysched values('BU1111', 16001, 20000, 18)
insert roysched values('BU1111', 20001, 24000, 20)
insert roysched values('BU1111', 24001, 28000, 22)
insert roysched values('BU1111', 28001, 50000, 24)
insert roysched values('MC2222', 0, 2000, 10)
insert roysched values('MC2222', 2001, 4000, 12)
insert roysched values('MC2222', 4001, 8000, 14)
insert roysched values('MC2222', 8001, 12000, 16)

GO

insert roysched values('MC2222', 12001, 20000, 18)
insert roysched values('MC2222', 20001, 50000, 20)
insert roysched values('TC7777', 0, 5000, 10)
insert roysched values('TC7777', 5001, 15000, 12)
insert roysched values('TC7777', 15001, 50000, 14)
insert roysched values('TC4203', 0, 2000, 10)
insert roysched values('TC4203', 2001, 8000, 12)
insert roysched values('TC4203', 8001, 16000, 14)
insert roysched values('TC4203', 16001, 24000, 16)
insert roysched values('TC4203', 24001, 32000, 18)

GO

insert roysched values('TC4203', 32001, 40000, 20)
insert roysched values('TC4203', 40001, 50000, 22)
insert roysched values('BU7832', 0, 5000, 10)
insert roysched values('BU7832', 5001, 10000, 12)
insert roysched values('BU7832', 10001, 15000, 14)
insert roysched values('BU7832', 15001, 20000, 16)
insert roysched values('BU7832', 20001, 25000, 18)
insert roysched values('BU7832', 25001, 30000, 20)
insert roysched values('BU7832', 30001, 35000, 22)
insert roysched values('BU7832', 35001, 50000, 24)

GO

insert roysched values('PS1372', 0, 10000, 10)
insert roysched values('PS1372', 10001, 20000, 12)
insert roysched values('PS1372', 20001, 30000, 14)
insert roysched values('PS1372', 30001, 40000, 16)
insert roysched values('PS1372', 40001, 50000, 18)

GO

raiserror('Now at the inserts to discounts ....',0,1)

GO

insert discounts values('Initial Customer', NULL, NULL, NULL, 10.5)
insert discounts values('Volume Discount', NULL, 100, 1000, 6.7)
insert discounts values('Customer Discount', '8042', NULL, NULL, 5.0)

GO

raiserror('Now at the inserts to jobs ....',0,1)

GO

insert jobs values ('New Hire - Job not specified', 10, 10)
insert jobs values ('Chief Executive Officer', 200, 250)
insert jobs values ('Business Operations Manager', 175, 225)
insert jobs values ('Chief Financial Officier', 175, 250)
insert jobs values ('Publisher', 150, 250)
insert jobs values ('Managing Editor', 140, 225)
insert jobs values ('Marketing Manager', 120, 200)
insert jobs values ('Public Relations Manager', 100, 175)
insert jobs values ('Acquisitions Manager', 75, 175)
insert jobs values ('Productions Manager', 75, 165)
insert jobs values ('Operations Manager', 75, 150)
insert jobs values ('Editor', 25, 100)
insert jobs values ('Sales Representative', 25, 100)
insert jobs values ('Designer', 25, 100)

GO

raiserror('Now at the inserts to employee ....',0,1)

GO

insert employee values ('PTC11962M', 'Philip', 'T', 'Cramer', 2, 215, '9952', '11/11/89')
insert employee values ('AMD15433F', 'Ann', 'M', 'Devon', 3, 200, '9952', '07/16/91')
insert employee values ('F-C16315M', 'Francisco', '', 'Chang', 4, 227, '9952', '11/03/90')
insert employee values ('LAL21447M', 'Laurence', 'A', 'Lebihan', 5, 175, '0736', '06/03/90')
insert employee values ('PXH22250M', 'Paul', 'X', 'Henriot', 5, 159, '0877', '08/19/93')
insert employee values ('SKO22412M', 'Sven', 'K', 'Ottlieb', 5, 150, '1389', '04/05/91')
insert employee values ('RBM23061F', 'Rita', 'B', 'Muller', 5, 198, '1622', '10/09/93')
insert employee values ('MJP25939M', 'Maria', 'J', 'Pontes', 5, 246, '1756', '03/01/89')
insert employee values ('JYL26161F', 'Janine', 'Y', 'Labrune', 5, 172, '9901', '05/26/91')
insert employee values ('CFH28514M', 'Carlos', 'F', 'Hernadez', 5, 211, '9999', '04/21/89')
insert employee values ('VPA30890F', 'Victoria', 'P', 'Ashworth', 6, 140, '0877', '09/13/90')
insert employee values ('L-B31947F', 'Lesley', '', 'Brown', 7, 120, '0877', '02/13/91')
insert employee values ('ARD36773F', 'Anabela', 'R', 'Domingues', 8, 100, '0877', '01/27/93')
insert employee values ('M-R38834F', 'Martine', '', 'Rance', 9, 75, '0877', '02/05/92')
insert employee values ('PHF38899M', 'Peter', 'H', 'Franken', 10, 75, '0877', '05/17/92')
insert employee values ('DBT39435M', 'Daniel', 'B', 'Tonini', 11, 75, '0877', '01/01/90')
insert employee values ('H-B39728F', 'Helen', '', 'Bennett', 12, 35, '0877', '09/21/89')
insert employee values ('PMA42628M', 'Paolo', 'M', 'Accorti', 13, 35, '0877', '08/27/92')
insert employee values ('ENL44273F', 'Elizabeth', 'N', 'Lincoln', 14, 35, '0877', '07/24/90')

GO

insert employee values ('MGK44605M', 'Matti', 'G', 'Karttunen', 6, 220, '0736', '05/01/94')
insert employee values ('PDI47470M', 'Palle', 'D', 'Ibsen', 7, 195, '0736', '05/09/93')
insert employee values ('MMS49649F', 'Mary', 'M', 'Saveley', 8, 175, '0736', '06/29/93')
insert employee values ('GHT50241M', 'Gary', 'H', 'Thomas', 9, 170, '0736', '08/09/88')
insert employee values ('MFS52347M', 'Martin', 'F', 'Sommer', 10, 165, '0736', '04/13/90')
insert employee values ('R-M53550M', 'Roland', '', 'Mendel', 11, 150, '0736', '09/05/91')
insert employee values ('HAS54740M', 'Howard', 'A', 'Snyder', 12, 100, '0736', '11/19/88')
insert employee values ('TPO55093M', 'Timothy', 'P', 'O''Rourke', 13, 100, '0736', '06/19/88')
insert employee values ('KFJ64308F', 'Karin', 'F', 'Josephs', 14, 100, '0736', '10/17/92')
insert employee values ('DWR65030M', 'Diego', 'W', 'Roel', 6, 192, '1389', '12/16/91')
insert employee values ('M-L67958F', 'Maria', '', 'Larsson', 7, 135, '1389', '03/27/92')
insert employee values ('PSP68661F', 'Paula', 'S', 'Parente', 8, 125, '1389', '01/19/94')
insert employee values ('MAS70474F', 'Margaret', 'A', 'Smith', 9, 78, '1389', '09/29/88')
insert employee values ('A-C71970F', 'Aria', '', 'Cruz', 10, 87, '1389', '10/26/91')
insert employee values ('MAP77183M', 'Miguel', 'A', 'Paolino', 11, 112, '1389', '12/07/92')
insert employee values ('Y-L77953M', 'Yoshi', '', 'Latimer', 12, 32, '1389', '06/11/89')
insert employee values ('CGS88322F', 'Carine', 'G', 'Schmitt', 13, 64, '1389', '07/07/92')
insert employee values ('PSA89086M', 'Pedro', 'S', 'Afonso', 14, 89, '1389', '12/24/90')
insert employee values ('A-R89858F', 'Annette', '', 'Roulet', 6, 152, '9999', '02/21/90')
insert employee values ('HAN90777M', 'Helvetius', 'A', 'Nagy', 7, 120, '9999', '03/19/93')
insert employee values ('M-P91209M', 'Manuel', '', 'Pereira', 8, 101, '9999', '01/09/89')
insert employee values ('KJJ92907F', 'Karla', 'J', 'Jablonski', 9, 170, '9999', '03/11/94')
insert employee values ('POK93028M', 'Pirkko', 'O', 'Koskitalo', 10, 80, '9999', '11/29/93')
insert employee values ('PCM98509F', 'Patricia', 'C', 'McKenna', 11, 150, '9999', '08/01/89')
GO

raiserror('Now at the create index section ....',0,1) with nowait

GO

CREATE CLUSTERED INDEX employee_ind ON employee(lname, fname, minit)

GO

CREATE NONCLUSTERED INDEX aunmind ON authors (au_lname, au_fname)
GO
CREATE NONCLUSTERED INDEX titleidind ON sales (title_id)
GO
CREATE NONCLUSTERED INDEX titleind ON titles (title)
GO
CREATE NONCLUSTERED INDEX auidind ON titleauthor (au_id)
GO
CREATE NONCLUSTERED INDEX titleidind ON titleauthor (title_id)
GO
CREATE NONCLUSTERED INDEX titleidind ON roysched (title_id)
GO

raiserror('Now at the create view section ....',0,1)

GO

CREATE VIEW titleview
AS
select title, au_ord, au_lname, price, ytd_sales, pub_id
from authors, titles, titleauthor
where authors.au_id = titleauthor.au_id
   AND titles.title_id = titleauthor.title_id

GO

raiserror('Now at the create procedure section ....',0,1)

GO

CREATE PROCEDURE byroyalty @percentage int
AS
select au_id from titleauthor
where titleauthor.royaltyper = @percentage

GO

raiserror('(Next is the first Grant embedded in the proc section.)',0,1)

GRANT execute ON byroyalty TO public

GO

CREATE PROCEDURE reptq1 AS
select pub_id, title_id, price, pubdate
from titles
where price is NOT NULL
order by pub_id
COMPUTE avg(price) BY pub_id
COMPUTE avg(price)

GO

GRANT execute ON reptq1 TO public

GO

CREATE PROCEDURE reptq2 AS
select type, pub_id, titles.title_id, au_ord,
   Name = substring (au_lname, 1,15), ytd_sales
from titles, authors, titleauthor
where titles.title_id = titleauthor.title_id AND authors.au_id = titleauthor.au_id
   AND pub_id is NOT NULL
order by pub_id, type
COMPUTE avg(ytd_sales) BY pub_id, type
COMPUTE avg(ytd_sales) BY pub_id

GO

GRANT execute ON reptq2 TO public

GO

CREATE PROCEDURE reptq3 @lolimit money, @hilimit money,
@type nchar(12)
AS
select pub_id, type, title_id, price
from titles
where price >@lolimit AND price <@hilimit AND type = @type OR type LIKE '%cook%'
order by pub_id, type
COMPUTE count(title_id) BY pub_id, type

GO

GRANT execute ON reptq3 TO public

GO

GRANT CREATE PROCEDURE TO public

GO

raiserror('Now at the GUEST section ....',0,1)

GO

/* Test to see if guest account needs to be added.
   The 7.X/8.0 version of the test has to query the
   sysusers.hasdbaccess column, which does not
   exist in earlier versions of SQL Server. Refering
   to this column on earlier versions would generate
   a compile error, so the reference is in a string
   that is only executed on 7.X/8.0 servers.  Could 
   not use sp_executesql because that is also not
   available on earlier versions.
*/
IF ( CHARINDEX('7.0', @@VERSION) > 0 OR
--     CHARINDEX('7.1', @@VERSION) > 0 OR
     CHARINDEX('7.5', @@VERSION) > 0 OR
     CHARINDEX('8.0', @@VERSION) > 0)
  EXEC ('IF NOT EXISTS (SELECT *
                        FROM sysusers
                        WHERE name = ''guest''
                        AND hasdbaccess = 1)
         EXEC sp_adduser ''guest'' ')
ELSE
  IF NOT EXISTS (SELECT *
                 FROM sysusers
                 WHERE name = 'guest')
  EXEC sp_adduser 'guest'
GO

GRANT ALL ON publishers TO guest
GRANT ALL ON pub_info TO guest
GRANT ALL ON employee TO guest
GRANT ALL ON jobs TO guest
GRANT ALL ON titles TO guest
GRANT ALL ON authors TO guest
GRANT ALL ON titleauthor TO guest
GRANT ALL ON sales TO guest
GRANT ALL ON roysched TO guest
GRANT ALL ON stores TO guest
GRANT ALL ON discounts TO guest
GRANT ALL ON titleview TO guest

GRANT execute ON byroyalty TO guest

GRANT CREATE TABLE TO guest
GRANT CREATE VIEW TO guest
GRANT CREATE RULE TO guest
GRANT CREATE DEFAULT TO guest
GRANT CREATE PROCEDURE TO guest

GO

UPDATE STATISTICS publishers
UPDATE STATISTICS employee
UPDATE STATISTICS jobs
UPDATE STATISTICS pub_info
UPDATE STATISTICS titles
UPDATE STATISTICS authors
UPDATE STATISTICS titleauthor
UPDATE STATISTICS sales
UPDATE STATISTICS roysched
UPDATE STATISTICS stores
UPDATE STATISTICS discounts

GO

CHECKPOINT

GO

USE master

GO

CHECKPOINT

GO

declare @dttm nvarchar(55)
select  @dttm=convert(nvarchar,getdate(),113)
raiserror('Ending InstPubs.SQL at %s ....',1,1,@dttm) with nowait

GO
-- -

