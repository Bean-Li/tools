#include <sstream>
#include <string>
#include <iostream>
#include <set>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "leveldb/db.h"

using namespace std;

void usage()
{
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "---------------------------------------------------------\n");
	fprintf(stdout, "ldbt -d db_path -o operation -p prefix -k key \n");
	fprintf(stdout, "-d  --db_path         specify the leveldb path \n");
	fprintf(stdout, "-o  --op              specify the operation (put or get or del or dump or dumpkey)\n");
	fprintf(stdout, "-p  --prefix          specify the key's prefix (if any) \n");
	fprintf(stdout, "-k  --key             specify the key \n");
	fprintf(stdout, "-v  --value           specify the value \n");

	return ;
}
string combine_strings(const string &prefix, const string &key)                                                                     
{

	if (prefix.size() == 0)
	{
		string out(key);
	    return out;
	}
	else
	{
		string out = prefix;
		out.push_back(0);
		out.append(key);
		return out;
	}
}


int db_get(string db_path , string prefix, string key, string* value)
{
	leveldb::Options options;
	leveldb::DB* db ;
	leveldb::Status status = leveldb::DB::Open(options, db_path, &db);

	int ret ;
	if(status.ok() == false)
	{
		cerr << "failed to open db " << db_path << endl;
		cerr << status.ToString() << endl;
		return 1;
	}

	string c_key = combine_strings(prefix, key);
	status = db->Get(leveldb::ReadOptions(),c_key, value);
	if(status.ok() == false)
	{
	    cerr << "failed to get " << key << endl;
		cerr << status.ToString() << endl;
		ret = 2;
	}
	else
	{
	    cout << "prefix: " << prefix << " key: " << key << "  value: " << *value <<endl;
	    ret =  0;	
	}

    delete db;
	return ret ;
}

int db_put(string db_path, string prefix, string key , string value)
{
	leveldb::Options options;
	leveldb::DB* db ;
	leveldb::Status status = leveldb::DB::Open(options, db_path, &db);

	int ret ;
	if(status.ok() == false)
	{
		cerr << "failed to open db " << db_path << endl;
		cerr << status.ToString() << endl;
		return 1;
	}

	string c_key = combine_strings(prefix, key);
	status = db->Put(leveldb::WriteOptions(),c_key, value);
	if(status.ok() == false)
	{
	    cerr << "failed to put " << key << endl;
		cerr << status.ToString() << endl;
		ret = 2;
	}
	else
	{
		cout << "put done!" ;
	    cout << "prefix: " << prefix << " key: " << key << "  value: " << value << endl;
	    ret =  0;	
	}

    delete db;
	return ret ;
}
int db_del(string db_path , string prefix, string key)
{
	leveldb::Options options;
	leveldb::DB* db ;
	leveldb::Status status = leveldb::DB::Open(options, db_path, &db);

	int ret ;
	if(status.ok() == false)
	{
		cerr << "failed to open db " << db_path << endl;
		cerr << status.ToString() << endl;
		return 1;
	}

	string c_key = combine_strings(prefix, key);
	status = db->Delete(leveldb::WriteOptions(),c_key);
	if(status.ok() == false)
	{
	    cerr << "failed to delete " << key << endl;
		cerr << status.ToString() << endl;
		ret = 2;
	}
	else
	{
		cout << "delete done!" << endl;
	    cout << "prefix: " << prefix << " key: " << key  <<endl;
	    ret =  0;	
	}

    delete db;
	return ret ;
}


int db_dump(string db_path , string prefix, bool key_only)
{
	leveldb::Options options;
	leveldb::DB* db ;
	leveldb::Status status = leveldb::DB::Open(options, db_path, &db);

	int ret ;
	bool prefix_flag = (prefix.size() != 0) ;
	if(status.ok() == false)
	{
		cerr << "failed to open db " << db_path << endl;
		cerr << status.ToString() << endl;
		return 1;
	}

	leveldb::Iterator* it = db->NewIterator(leveldb::ReadOptions());
	if(prefix_flag == false) 
	{
		for (it->SeekToFirst(); it->Valid(); it->Next())
		{
			if(key_only == false)
			    cout << it->key().ToString() << " : " << it->value().ToString() << endl;
			else
				cout << it->key().ToString() << endl ;
		}
	}
	else
	{
		for(it->Seek(prefix); it->Valid() && it->key().starts_with(prefix) ; it->Next())    
		{
			if(key_only == false)
			    cout << it->key().ToString() << " : " << it->value().ToString() << endl;
			else
				cout << it->key().ToString() << endl ;
		}
	}

	if(it->status().ok() == false)
	{
		cerr << "An error was found during the scan" << endl;
		cerr << it->status().ToString() << endl; 
		ret =  -1;
	}
	else
	{
		cout << "dump done!" << endl;
	    ret =  0;	
	}

    delete db;
	return ret ;
}
int main(int argc, char* argv[])
{
	char db_path[1024];
	char current_path[1024];
	char op[256] ; 

	char prefix[1024+1];
	char key[1024+1];
	char value[20480+1];

	static struct option long_options[] = {
		{"db_path", required_argument, 0 , 'd'},
		{"op", required_argument, 0 , 'o'},
		{"prefix", required_argument, 0 , 'p'},
		{"key", required_argument, 0 , 'k'},
		{"value", required_argument, 0 , 'v'},
		{0,0,0,0}
	};

	int ch ; 
	int option_index = 0;
	char* res = NULL ;
	int ret = 0;
	struct stat statbuf;

	memset(db_path,'\0', sizeof(db_path));
	memset(op,'\0', sizeof(op));
	memset(prefix, '\0', sizeof(prefix));
	memset(key, '\0', sizeof(key));

	while((ch = getopt_long(argc,argv, "h?d:o:p:k:v:", long_options,&option_index)) != -1)
	{
		switch(ch)
		{
		case 'd':
			res = realpath(optarg,db_path);
			if(!res)
			{
				fprintf(stderr, "failed to get realpath of db_path\n");
				exit(1);
			}
			ret = stat(db_path, &statbuf);
			if(ret != 0)
			{
				fprintf(stderr, "failed to stat %s\n", db_path);
				exit(1);
			}

			if(!S_ISDIR(statbuf.st_mode))
			{
				fprintf(stderr, "%s is not a directory\n",db_path);
				exit(1);
			}
			snprintf(current_path,1024,"%s/CURRENT",db_path);

			ret = stat(current_path,&statbuf);
			if(ret != 0)
			{
				fprintf(stderr, "CURRENT file is need in the db path, failed to stat %s\n", current_path);
				exit(1);
			}

			if(!S_ISREG(statbuf.st_mode))
			{
				fprintf(stderr, "%s is not a file\n",db_path);
				exit(1);
			}
			break ;

		case 'o':
			strncpy(op,optarg,255);
			if(strcmp(op,"put") != 0   && 
		       strcmp(op, "get") != 0  &&
			   strcmp(op, "del") != 0  &&
			   strcmp(op, "dump") != 0 &&
			   strcmp(op, "dumpkey") != 0)
			{
				fprintf(stderr,"invalid op :%s, we only support (put, get, del)\n", op);
				exit(2);
			}
			break;
		case 'p':
			strncpy(prefix, optarg,1024);
			break;
		case 'k':
			strncpy(key, optarg,1024);
			break;
		case 'v':
			strncpy(value, optarg,20480);
			break;
		case 'h':
		case '?':
			usage();
			return 0;
			break;

		}
	}

	if(strlen(db_path) == 0)
	{
		fprintf(stderr, "You must specify the db path\n");
		usage();
		exit(1);
	}
	if(strlen(op) == 0)
	{
		fprintf(stderr, "You must specify the op, put or get is valid\n");
		usage();
		exit(1);
	}

	if(strlen(key) == 0 && strcmp(op, "dump") && strcmp(op, "dumpkey"))
	{
		fprintf(stderr, "You must specify the key, prefix is optional\n");
		usage();
		exit(1);
	}

	if(strcmp(op, "put") == 0 && strlen(value) ==0)
	{
		fprintf(stderr, "value is needed if the op is put\n") ;
		usage();
		exit(1);
	}

	string s_prefix(prefix);
	string s_key(key) ;


	string s_db_path(db_path);
	
	if(strcmp(op, "get") == 0)
	{
		string s_value ;
	    ret = db_get(s_db_path, s_prefix, s_key, &s_value);
	}
	else if(strcmp(op, "put") == 0)
	{
		string s_value(value);
	    ret = db_put(s_db_path,prefix, key, s_value);
	}
	else if(strcmp(op, "del") == 0)
	{
	    ret = db_del(s_db_path, s_prefix, s_key);
	}
	else if (strcmp(op, "dump") == 0)
	{
	    ret = db_dump(s_db_path, s_prefix, false);
	}
	else if (strcmp(op, "dumpkey") == 0)
	{
	    ret = db_dump(s_db_path, s_prefix, true);
	}
	else
	{
		cerr<< "op " << op << " is not supported yet" <<endl;
	    ret = -1;
	}

	return ret;

}


