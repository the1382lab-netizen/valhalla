/**
 * Type declarations for sql.js
 * @see https://sql.js.org/documentation/
 */
declare module 'sql.js' {
  export interface QueryExecResult {
    columns: string[];
    values: any[][];
  }

  export interface Database {
    run(sql: string, params?: any[]): Database;
    exec(sql: string, params?: any[]): QueryExecResult[];
    export(): Uint8Array;
    close(): void;
  }

  export interface SqlJsStatic {
    Database: new (data?: ArrayLike<number> | Buffer | null) => Database;
  }

  export default function initSqlJs(config?: any): Promise<SqlJsStatic>;
}
