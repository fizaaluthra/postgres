/*
 *	version.c
 *
 *	Postgres-version-specific routines
 *
 *	Copyright (c) 2010-2026, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/version.c
 */

#include "postgres_fe.h"

#include "fe_utils/string_utils.h"
#include "pg_upgrade.h"

/*
 * version_hook functions for check_for_data_types_usage in order to determine
 * whether a data type check should be executed for the cluster in question or
 * not.
 */
bool
jsonb_9_4_check_applicable(ClusterInfo *cluster)
{
	/* JSONB changed its storage format during 9.4 beta */
	if (GET_MAJOR_VERSION(cluster->major_version) == 904 &&
		cluster->controldata.cat_ver < JSONB_FORMAT_CHANGE_CAT_VER)
		return true;

<<<<<<< HEAD
	return false;
=======
	for (dbnum = 0; dbnum < cluster->dbarr.ndbs; dbnum++)
	{
		DbInfo	   *active_db = &cluster->dbarr.dbs[dbnum];
		PGconn	   *conn = connectToServer(cluster, active_db->db_name);
		PQExpBufferData querybuf;
		PGresult   *res;
		bool		db_used = false;
		int			ntups;
		int			rowno;
		int			i_nspname,
					i_relname,
					i_attname;

		/*
		 * The type(s) of interest might be wrapped in a domain, array,
		 * composite, or range, and these container types can be nested (to
		 * varying extents depending on server version, but that's not of
		 * concern here).  To handle all these cases we need a recursive CTE.
		 */
		initPQExpBuffer(&querybuf);
		appendPQExpBuffer(&querybuf,
						  "WITH RECURSIVE oids AS ( "
		/* start with the type(s) returned by base_query */
						  "	%s "
						  "	UNION ALL "
						  "	SELECT * FROM ( "
		/* inner WITH because we can only reference the CTE once */
						  "		WITH x AS (SELECT oid FROM oids) "
		/* domains on any type selected so far */
						  "			SELECT t.oid FROM pg_catalog.pg_type t, x WHERE typbasetype = x.oid AND typtype = 'd' "
						  "			UNION ALL "
		/* arrays over any type selected so far */
						  "			SELECT t.oid FROM pg_catalog.pg_type t, x WHERE typelem = x.oid AND typtype = 'b' "
						  "			UNION ALL "
		/* composite types containing any type selected so far */
						  "			SELECT t.oid FROM pg_catalog.pg_type t, pg_catalog.pg_class c, pg_catalog.pg_attribute a, x "
						  "			WHERE t.typtype = 'c' AND "
						  "				  t.oid = c.reltype AND "
						  "				  c.oid = a.attrelid AND "
						  "				  NOT a.attisdropped AND "
						  "				  a.atttypid = x.oid "
						  "			UNION ALL "
		/* ranges containing any type selected so far */
						  "			SELECT t.oid FROM pg_catalog.pg_type t, pg_catalog.pg_range r, x "
						  "			WHERE t.typtype = 'r' AND r.rngtypid = t.oid AND r.rngsubtype = x.oid"
						  "	) foo "
						  ") "
		/* now look for stored columns of any such type */
						  "SELECT n.nspname, c.relname, a.attname "
						  "FROM	pg_catalog.pg_class c, "
						  "		pg_catalog.pg_namespace n, "
						  "		pg_catalog.pg_attribute a "
						  "WHERE	c.oid = a.attrelid AND "
						  "		NOT a.attisdropped AND "
						  "		a.atttypid IN (SELECT oid FROM oids) AND "
						  "		c.relkind IN ("
						  CppAsString2(RELKIND_RELATION) ", "
						  CppAsString2(RELKIND_MATVIEW) ", "
						  CppAsString2(RELKIND_INDEX) ") AND "
						  "		c.relnamespace = n.oid AND "
		/* exclude possible orphaned temp tables */
						  "		n.nspname !~ '^pg_temp_' AND "
						  "		n.nspname !~ '^pg_toast_temp_' AND "
		/* exclude system catalogs, too */
						  "		n.nspname NOT IN ('pg_catalog', 'information_schema')",
						  base_query);

		res = executeQueryOrDie(conn, "%s", querybuf.data);

		ntups = PQntuples(res);
		i_nspname = PQfnumber(res, "nspname");
		i_relname = PQfnumber(res, "relname");
		i_attname = PQfnumber(res, "attname");
		for (rowno = 0; rowno < ntups; rowno++)
		{
			found = true;
			if (script == NULL && (script = fopen_priv(output_path, "w")) == NULL)
				pg_fatal("could not open file \"%s\": %s\n", output_path,
						 strerror(errno));
			if (!db_used)
			{
				yb_fprintf_and_log(script, "In database: %s\n", active_db->db_name);
				db_used = true;
			}
			yb_fprintf_and_log(script, "  %s.%s.%s\n",
							   PQgetvalue(res, rowno, i_nspname),
							   PQgetvalue(res, rowno, i_relname),
							   PQgetvalue(res, rowno, i_attname));
		}

		PQclear(res);

		termPQExpBuffer(&querybuf);

		PQfinish(conn);
	}

	if (script)
		fclose(script);

	return found;
>>>>>>> bc662ba7050
}

/*
 * Older servers can't support newer protocol versions, so their connection
 * strings will need to lock max_protocol_version to 3.0.
 */
bool
protocol_negotiation_supported(const ClusterInfo *cluster)
{
	/*
	 * The February 2018 patch release (9.3.21, 9.4.16, 9.5.11, 9.6.7, and
	 * 10.2) added support for NegotiateProtocolVersion. But ClusterInfo only
	 * has information about the major version number. To ensure we can still
	 * upgrade older unpatched servers, just assume anything prior to PG11
	 * can't negotiate. It's not possible for those servers to make use of
	 * newer protocols anyway, so nothing is lost.
	 */
	return (GET_MAJOR_VERSION(cluster->major_version) >= 1100);
}

/*
 * old_9_6_invalidate_hash_indexes()
 *	9.6 -> 10
 *	Hash index binary format has changed from 9.6->10.0
 */
void
old_9_6_invalidate_hash_indexes(ClusterInfo *cluster, bool check_mode)
{
	int			dbnum;
	FILE	   *script = NULL;
	bool		found = false;
	char	   *output_path = "reindex_hash.sql";

	prep_status("Checking for hash indexes");

	for (dbnum = 0; dbnum < cluster->dbarr.ndbs; dbnum++)
	{
		PGresult   *res;
		bool		db_used = false;
		int			ntups;
		int			rowno;
		int			i_nspname,
					i_relname;
		DbInfo	   *active_db = &cluster->dbarr.dbs[dbnum];
		PGconn	   *conn = connectToServer(cluster, active_db->db_name);

		/* find hash indexes */
		res = executeQueryOrDie(conn,
								"SELECT n.nspname, c.relname "
								"FROM	pg_catalog.pg_class c, "
								"		pg_catalog.pg_index i, "
								"		pg_catalog.pg_am a, "
								"		pg_catalog.pg_namespace n "
								"WHERE	i.indexrelid = c.oid AND "
								"		c.relam = a.oid AND "
								"		c.relnamespace = n.oid AND "
								"		a.amname = 'hash'"
			);

		ntups = PQntuples(res);
		i_nspname = PQfnumber(res, "nspname");
		i_relname = PQfnumber(res, "relname");
		for (rowno = 0; rowno < ntups; rowno++)
		{
			found = true;
			if (!check_mode)
			{
				if (script == NULL && (script = fopen_priv(output_path, "w")) == NULL)
					pg_fatal("could not open file \"%s\": %m", output_path);
				if (!db_used)
				{
					PQExpBufferData connectbuf;

					initPQExpBuffer(&connectbuf);
					appendPsqlMetaConnect(&connectbuf, active_db->db_name);
					fputs(connectbuf.data, script);
					termPQExpBuffer(&connectbuf);
					db_used = true;
				}
				fprintf(script, "REINDEX INDEX %s.%s;\n",
						quote_identifier(PQgetvalue(res, rowno, i_nspname)),
						quote_identifier(PQgetvalue(res, rowno, i_relname)));
			}
		}

		PQclear(res);

		if (!check_mode && db_used)
		{
			/* mark hash indexes as invalid */
			PQclear(executeQueryOrDie(conn,
									  "UPDATE pg_catalog.pg_index i "
									  "SET	indisvalid = false "
									  "FROM	pg_catalog.pg_class c, "
									  "		pg_catalog.pg_am a, "
									  "		pg_catalog.pg_namespace n "
									  "WHERE	i.indexrelid = c.oid AND "
									  "		c.relam = a.oid AND "
									  "		c.relnamespace = n.oid AND "
									  "		a.amname = 'hash'"));
		}

		PQfinish(conn);
	}

	if (script)
		fclose(script);

	if (found)
	{
		report_status(PG_WARNING, "warning");
		if (check_mode)
			pg_log(PG_WARNING, "\n"
				   "Your installation contains hash indexes.  These indexes have different\n"
				   "internal formats between your old and new clusters, so they must be\n"
				   "reindexed with the REINDEX command.  After upgrading, you will be given\n"
				   "REINDEX instructions.");
		else
			pg_log(PG_WARNING, "\n"
				   "Your installation contains hash indexes.  These indexes have different\n"
				   "internal formats between your old and new clusters, so they must be\n"
				   "reindexed with the REINDEX command.  The file\n"
				   "    %s\n"
				   "when executed by psql by the database superuser will recreate all invalid\n"
				   "indexes; until then, none of these indexes will be used.",
				   output_path);
	}
	else
		check_ok();
}

/*
 * Callback function for processing results of query for
 * report_extension_updates()'s UpgradeTask.  If the query returned any rows,
 * write the details to the report file.
 */
static void
process_extension_updates(DbInfo *dbinfo, PGresult *res, void *arg)
{
	int			ntups = PQntuples(res);
	int			i_name = PQfnumber(res, "name");
	UpgradeTaskReport *report = (UpgradeTaskReport *) arg;
	PQExpBufferData connectbuf;

	if (ntups == 0)
		return;

	if (report->file == NULL &&
		(report->file = fopen_priv(report->path, "w")) == NULL)
		pg_fatal("could not open file \"%s\": %m", report->path);

<<<<<<< HEAD
	initPQExpBuffer(&connectbuf);
	appendPsqlMetaConnect(&connectbuf, dbinfo->db_name);
	fputs(connectbuf.data, report->file);
	termPQExpBuffer(&connectbuf);

	for (int rowno = 0; rowno < ntups; rowno++)
		fprintf(report->file, "ALTER EXTENSION %s UPDATE;\n",
				quote_identifier(PQgetvalue(res, rowno, i_name)));
=======
	if (check_for_data_type_usage(cluster, "information_schema.sql_identifier",
								  output_path))
	{
		if (!is_yugabyte_enabled())
			pg_log(PG_REPORT, "fatal\n");
		yb_fatal("Your installation contains the \"sql_identifier\" data type in user tables.\n"
				 "The on-disk format for this data type has changed, so this\n"
				 "cluster cannot currently be upgraded.  You can\n"
				 "drop the problem columns and restart the upgrade.\n"
				 "A list of the problem columns is printed above and in the file:\n"
				 "    %s\n\n", output_path);
	}
	else
		check_ok();
>>>>>>> bc662ba7050
}

/*
 * report_extension_updates()
 *	Report extensions that should be updated.
 */
void
report_extension_updates(ClusterInfo *cluster)
{
	UpgradeTaskReport report;
	UpgradeTask *task = upgrade_task_create();
	const char *query = "SELECT name "
		"FROM pg_available_extensions "
		"WHERE installed_version != default_version";

	prep_status("Checking for extension updates");

	report.file = NULL;
	strcpy(report.path, "update_extensions.sql");

	upgrade_task_add_step(task, query, process_extension_updates,
						  true, &report);

	upgrade_task_run(task, cluster);
	upgrade_task_free(task);

	if (report.file)
	{
		fclose(report.file);
		report_status(PG_REPORT, "notice");
		pg_log(PG_REPORT, "\n"
			   "Your installation contains extensions that should be updated\n"
			   "with the ALTER EXTENSION command.  The file\n"
			   "    %s\n"
			   "when executed by psql by the database superuser will update\n"
			   "these extensions.",
			   report.path);
	}
	else
		check_ok();
}
