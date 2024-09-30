#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

// 3 Versions can be saved and accessed at a time
#define MAX_VERS 3

/**
 * Ensure the root directory "vctl" exists. This will be the root directory for all future
 * subdirectories (filenames) which in turn contain versions of the specified filename.
 * If it doesn't exist, create it now.
 * @return 0 or -1: root exists/created successfully or failed
 */
int ensure_Vctl_Dir(){
	// Try to open the /vctl directory 	
	if (open("/vctl", O_RDONLY) < 0) {
		// If /vctl doesn't exist, create it now
		if (mkdir("/vctl") < 0) {
			printf("Error: /vctl directory failed to be created\n");
			return -1;
		}
	}
	return 0;
}

/**
 * Construct the file path that can be used to access and work within directories
 * root + filename
 * @param filename: the name of the file we are working with 
 * @return filePath: the constructed file path for general file directory ex: /vctl/filename
 */
char* constructFile_Path(char *filename) {
	char *root = "/vctl/";
	char *filePath;

	int pathSize = strlen(root) + strlen(filename) + 1;
	filePath = (char *) malloc(pathSize);

	if (filePath == NULL) {
		printf("Error: Memory allocation has failed\n");
		return NULL;
	}
	
	strcpy(filePath, root); // copy "/vctl/"
	strcpy(filePath + strlen(root), filename); // append the filename

	return filePath;
}

/**
 * Ensure the directory for the filename exists already and if not create it.
 * @param filename: the name of the file we are working with 
 * @return 0 or -1: directory of a certain file exists/created successfully or failed 
 */
int ensure_Ver_Dir(char *filename) {
	// Time to construct the path inside the root directory "vctl" to the new directory 
	// that will hold the versions of a certain file 
	// ex: /vctl/textfile.txt -> inside would be textfileV1, textfileV2 etc.
	char *dir = constructFile_Path(filename);

	// Try to open directory and if it doesn't exist, create it
	if (open(dir, O_RDONLY) < 0) {
		if (mkdir(dir) < 0) {
			printf("Error: /vctl/%s failed to be created\n", filename);
			free(dir);
			return -1;
		}
	}
	free(dir);
	return 0;
}

/**
 * Helper function to identify valid count of versions saved in struct
 * @param str: name of entire file version saved
 * @param prefix: "Version" is what we want to make sure str starts with to be able to count
 * how may file versions exist in the current directory 
 * @return 0 or 1: prefix matches or not
 */
int startsWith(char *str, char *prefix) {
	while (*prefix) {
		if (*str != *prefix) {
			return 0; // strings are not equal
		}
		str++;
		prefix++;
	}
	return 1; // prefix does match
}

/**
 * Function to retrieve the current total of versions within the directory: 1, 2, or 3.
 * @param filename: current file name
 * @return verCount: total number of versions currently in directory
 */
int retrieveVer_Count(char *filename) {
	char buf[512];
	int fd;
	struct dirent de;
	struct stat st;
	int verCount = 0;

	// Construct the path to the file's ver directory 
	// ex: /vctl/filename
	char *dir = constructFile_Path(filename);

	// Try to open directory that holds the specific file versions and make sure
	// the directory exists and entries are valid (is a directory)
	if ((fd = open(dir, O_RDONLY)) < 0) {
		printf("Error: failed to open directory %s\n", dir);
		free(dir);
		return -1;
	}
	if (fstat(fd, &st) < 0 || st.type != T_DIR) {
		printf("Error: %s is not a directory\n", dir);
		close(fd);
		free(dir);
		return -1;
	}
	// Even though the version limit is 3, there are still possibly empty entires in
	// the directory if a file was deleted or replaced so we don't want to invalidate the count
	while (read(fd, &de, sizeof(de)) == sizeof(de)) {
		if (de.inum == 0) {
			continue;
		}
		int dirLength = strlen(dir);
		int deName_Length = strlen(de.name);

		if (dirLength + 1 + deName_Length + 1 > sizeof(buf)) {
			printf("Error: Path Buffer Overflow\n");
			continue;
		}

		strcpy(buf, dir); // /vctl/filename
		buf[dirLength] = '/'; // /vctl/filename/
		strcpy(buf + dirLength + 1, de.name); // /vctl/filename/Version2_testfile.txt
		buf[dirLength + 1 + deName_Length] = '\0';

		if (stat(buf, &st) < 0) {
			printf("Error: unable to stat %s\n", buf);
			continue;
		}
		if (st.type == T_FILE && startsWith(de.name, "Version")) {
			verCount++;
		}
	}
	close(fd);
	free(dir);

	return verCount;
}

/**
 * Helper function to construct the full file version path to be used
 * @param filename: the current filename
 * @param verNum: the version number 1-3
 * @return verPath: return the full constructed file version path ex: /vctl/filename/Version#_filename
 */
char* constructVer_Path(char *filename, int verNum) {
	char *root = "/vctl/";
	char verPrefix[16]; // buffer for version number portion /Version#_
	char *verPath; // pointer for final version path

	if (verNum == 1) {
		strcpy(verPrefix, "/Version1_");
	} else if (verNum == 2) {
		strcpy(verPrefix, "/Version2_");
	} else if (verNum == 3) {
		strcpy(verPrefix, "/Version3_");
	}

	int pathSize = strlen(root) + strlen(filename) + strlen(verPrefix) + strlen(filename) + 1; //null term
	verPath = (char *) malloc(pathSize);

	if (verPath == NULL) {
		printf("Error: Memory allocation has failed\n");
		return NULL;
	}

	// Construct the full path for filename and versions
	strcpy(verPath, root);
	strcpy(verPath + strlen(root), filename);
	strcpy(verPath + strlen(root) + strlen(filename), verPrefix);
	strcpy(verPath + strlen(root) + strlen(filename) + strlen(verPrefix), filename);

	return verPath;
}
 
/**
 * Function that will implement that -saveVersion command/flag
 * 		- Check that all root directories and subdirectories have been made
 * 		- Check how many versions of the file there exists in the directory already
 *		- If 3 already exist, overwrite of oldest version must occur and all remaining versions must shift up if user approves
 * 		- All remaining versions must be renamed and the newest version must be saved in last slot now (Ver3)
 *		- If less than 3 exist, then save the version into the next open slot in the directory
 * @param filename: file we are working with
 */
void saveVersion(char *filename){
	int verCount;
	char decision[32];
	char *verPath;
	
	// Ensure the root directory to hold all file version directories exist
	if (ensure_Vctl_Dir() < 0) {
		return;
	}
	// Ensure the specific file has an existing version directory 
	if (ensure_Ver_Dir(filename) < 0) {
		return;
	}
	verCount = retrieveVer_Count(filename);

	if(verCount >= MAX_VERS) {
		printf("Maximum number of versions reached for this file. Do you want to overwrite the oldest version? (y/n): ");
		read(0, decision, sizeof(decision));
		if (decision[0] != 'y') {
			return;
		}
		// Overwrite oldest version and prepare all other files to update version numbers (shift forwards)

		char temp[512]; // buff to hold the content ver temporarily
		int fd, bytesRead;

		char *ver1 = constructVer_Path(filename, 1);
		char *ver2 = constructVer_Path(filename, 2);
		char *ver3 = constructVer_Path(filename, 3);

		if (!ver1 || !ver2 || !ver3) {
			printf("Error: Constructing version paths has failed\n");
			return;
		}

		if (open(ver1, O_RDONLY) >= 0) {
			// Write Version2 -> Version1
			if ((fd = open(ver2, O_RDONLY)) >= 0) {
				// O_TRUNC is to make size 0 to ensure everything is overwritten and no
				// leftover bytes remain in new version 
				int fd_new = open(ver1, O_CREATE | O_WRONLY | O_TRUNC);
				if (fd_new < 0) {
					printf("Error: Failed to shift Ver2 -> Ver1\n");
					free(ver1); free(ver2); free(ver3);
					return;
				}
				while ((bytesRead = read(fd, temp, sizeof(temp))) > 0) {
					write(fd_new, temp, bytesRead);
				}
				close(fd);
				close(fd_new);
			}

			// Write Version3 -> Version2
			if ((fd = open(ver3, O_RDONLY)) >= 0) {
				int fd_new = open(ver2, O_CREATE | O_WRONLY | O_TRUNC);
				if (fd_new < 0) {
					printf("Error: Failed to shift Ver3 -> Ver2\n");
					free(ver1); free(ver2); free(ver3);
					return;
				}
				while ((bytesRead = read(fd, temp, sizeof(temp))) > 0) {
					write(fd_new, temp, bytesRead);
				}
				close(fd);
				close(fd_new);
			}

			//Write newest Version to become Version3
			int fd_new = open(ver3, O_CREATE | O_WRONLY | O_TRUNC);
			if (fd_new < 0) {
				printf("Error: Failed to create newest Version, check for invalid file name\n");
				free(ver1); free(ver2); free(ver3);
				return;
			}
			int fd_source = open(filename, O_RDONLY);
			if (fd_source < 0) {
				printf("Error: Failed to open source file, check for invalid file name\n");
				close(fd_new);
				free(ver1); free(ver2); free(ver3);
				return;
			}
			while ((bytesRead = read(fd_source, temp, sizeof(temp))) > 0) {
				write(fd_new, temp, bytesRead);
			}
			close(fd_source);
			close(fd_new);
			printf("Newest version of %s saved\n", filename);
		}
		
		free(ver1); free(ver2); free(ver3);
	} else {
		// If MAX_VERS has not been reached yet, save version in the next open slot
		char temp[512]; // buff to hold the content ver temporarily
		int bytesRead;
		verPath = constructVer_Path(filename, verCount + 1);

		if (!verPath) {
			printf("Error: Constructing version path has failed\n");
			return;
		}
		printf("Version path is %s\n", verPath);
		int newVer = open(verPath, O_CREATE | O_WRONLY);
		if (newVer < 0) {
			printf("Error: Failed to save newest Version, check for invalid file name\n");
			free(verPath);
			return;
		}
		int fd_source = open(filename, O_RDONLY);
		if (fd_source < 0) {
			printf("Error: Failed to open source file, file invalid\n");
			close(newVer);
			free(verPath);
			return;
		}
		while ((bytesRead = read(fd_source, temp, sizeof(temp))) > 0) {
			write(newVer, temp, bytesRead);
		}
		close(fd_source);
		close(newVer);
		free(verPath);
		printf("Current Version of %s has been saved\n", filename);
	
	}
}

/**
 * Function that will implement the -listVersions command/flag:
 * 		- Opens the specified filename's directory and checks for existence
 * 		- Proceeds to print all existing version names saved in the directory
 * @param filename: file we are working with
 */
void listVersions(char *filename){
	// open the directory and list the versions 
	char *dir = constructFile_Path(filename);

	int fd = open(dir, O_RDONLY);
	if (fd < 0) {
		printf("Error: Failed to open directory/file, please check if directory exists in mkdir as there might be no past versions of this file %s, or file doesn't exist\n", dir);
		free(dir);
		return;
	}
	struct dirent de;
	struct stat st;
	printf("Versions of %s:\n", filename);
	while(read(fd, &de, sizeof(de)) == sizeof(de)) {
		if (de.inum == 0) {
			continue;
		}
		char buf[512];
		int dirLength = strlen(dir);
		int deName_Length = strlen(de.name);
		
		if (dirLength + 1 + deName_Length + 1 > sizeof(buf)) {
			printf("Error: Path Buffer Overflow\n");
			continue;
		}
		
		strcpy(buf, dir); // /vctl/filename
		buf[dirLength] = '/'; // /vctl/filename/
		strcpy(buf + dirLength + 1, de.name); // /vctl/filename/Version2_testfile.txt
		buf[dirLength + 1 + deName_Length] = '\0';

		if (stat(buf, &st) < 0) {
			printf("Error: unable to stat %s\n", buf);
			continue;
		}
		if (st.type == T_FILE && startsWith(de.name, "Version")) {
			printf("%s\n", de.name);
		}
	}
	close(fd);
	free(dir);
}

/**
 * Helper function that will check if the entered file name version is valid 
 * @param: str: ex: Version2_
 * @return 0 or 1: if both version and number are valid
 */
int isValidVersion (char *str) {
	int i = 0;
	while (str[i] != '\0') {
		i++;
	}
	if (i < 8) {
		return 0; // invalid since Version is 7 and + 1 digit
	}
	if ((str[0] != 'V' && str[0] != 'v') ||
	    (str[1] != 'E' && str[1] != 'e') ||
	    (str[2] != 'R' && str[2] != 'r') ||
	    (str[3] != 'S' && str[3] != 's') ||
	    (str[4] != 'I' && str[4] != 'i') ||
	    (str[5] != 'O' && str[5] != 'o') ||
	    (str[6] != 'N' && str[6] != 'n')) {
	    	return 0;
	}
	if (str[7] < '0' || str[7] > '4') {
		return 0; // invalid version number
	}
	// else if all checks pass this is valid
	return 1;
}

/**
 * Helper function to extract just the filename from the full version file name
 * @param str: Version2_textfile.txt
 * @return &str[i + 1] : everything that comes after the '_' character is the filename
 */
char *extract_fileName (char *str) {
	int i = 0;
	while (str[i] != '\0') {
		if (str[i] == '_') {
			return &str[i+1];
		}
		i++;
	}
	printf("Invalid version name provided, missing '_'\n");
	return 0; 
}

/**
 * Helper function to extract just the version number from the full version file name
 * @param str: Version2_textfile.txt
 * @return verNum: return any valid version number (1-3); returns -1 for invalid number
 */
int extract_verNum(char *str) {
	int i = 0;
	while (str[i] != '_' && str[i] != '\0') {
		i++;
	}
	if (i > 7 && str[i - 1] >= '1' && str[i - 1] <= '3') {
			int verNum = str[i - 1] - '0';
			return verNum;
	}
	printf("Invalid version number providing missing number 1-3\n");
	return -1;
}

/**
 * Function that will implement the -viewVersion command/flag:
 * 		- Check that all root and subdirectories for the file exist
 * 		- Check that the provided version is correct
 *		- Check that the provided version number is correct
 * 		- Get the entire version path for the specified file and open its content
 * 		- Read and write the file content to the output like cat filename
 * @param: ver_filename: Version2_textfile.txt
 */
void viewVersion(char *ver_filename){
	char *filename = extract_fileName(ver_filename);
	
	if (ensure_Vctl_Dir() < 0) {
		return;
	}
	if (ensure_Ver_Dir(filename) < 0) {
		printf("Error: Directory does not exist\n");
		return;
	}
	if (isValidVersion(ver_filename) == 0) {
		printf("Error: Invalid version name provided\n");
		return;
	}
	if (extract_verNum(ver_filename) < 0) {
		printf("Error: Invalid version number provided\n");
		return;
	}

	int verNum = extract_verNum(ver_filename);
	printf("Version number is: %d\n", verNum);

	char *verPath = constructVer_Path(filename, verNum);
	int verCount = retrieveVer_Count(filename);

	int fd = open(verPath, O_RDONLY);
	if (fd < 0) {
		printf("Error: Failed to open version %d of %s\n", verNum, filename);
		printf("There exists: %d total versions under this file name. Ensure Version and filename are valid\n", verCount);
		free(verPath);
		return;
	}
	
	char temp[512];
	int bytesRead;
	while((bytesRead = read(fd, temp, sizeof(temp))) > 0) {
		write(1, temp, bytesRead);
	}
	close(fd);
	free(verPath);
}

/**
 * Check to ensure the correct number of arguments are provided in the command line and provide options for user 
 * Compare arguments to then respond and call associated functions that handle the flags entered
 * If invalid flags are inputted, give user information on the valid entries
 */
int main(int argc, char *argv[]) {
	if (argc <= 2) {
		printf("Invalid number of arguments provided: Examples below: \n"
				"/t vcp -listVersions textfile.txt \n"
				"/t vcp -viewVersion Version1_textfile.txt\n"
				"/t vcp -saveVersion textfile.txt\n");
		exit(1);
	}
	if (strcmp(argv[1], "-listVersions") == 0) {
		listVersions(argv[2]);
	} else if (strcmp(argv[1],"-viewVersion") == 0) {
		viewVersion(argv[2]);
	} else if (strcmp(argv[1], "-saveVersion") == 0) {
		saveVersion(argv[2]);
	} else {
		printf("Sorry, please insert valid flag/command: -listVersions, -viewVersion, -saveVersion; followed by file name as next arg\n");
		return 0;		
	}
	return 0;
}
